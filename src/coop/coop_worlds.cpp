#include <nlohmann/json.hpp> // coop-pt1111e: the quests' progress slots (first: a game header's macro breaks it after)
#include "coop/game_ns.hpp"
#include "coop/coop_worlds.hpp"
#include "coop/netplay_session.hpp" // coop-session
#include "coop/netplay_cmd.hpp" // coop-localmenu: recorded commands

#include <libvu0.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <string>
#include <unordered_set>
#include <vector>

#include "btmisc.hpp"
#include "camera.hpp"
#include "camerafollow.hpp" // coop-cutsceneview: the scene's camera
#include "character.hpp"
#include "dngstatusdata.hpp"
#include "dungeoneventdata.hpp"
#include "dispctrl.hpp"
#include "dungeoneventman.hpp"
#include "dungeonmap.hpp" // coop-dungeonp2: the floor layout line
#include "edit.hpp" // coop-housemerge: ED_EVENT_PARAM, for GotoInterior
#include "ebattle.hpp" // coop-dunfreeze: EdPadDown, as a door event's script reads it
#include "editground.hpp" // coop-flagsync: StepWater
#include "editarea.hpp"      // coop-geopart: the place test's cells
#include "editpartsinfo.hpp" // coop-geopart: the place test's catalogue
#include "mapparts.hpp"
#include "editloop.hpp" // coop-p2scene: EdInteriorName
#include "editloop3.hpp" // coop-floorgate: EdFadeOut, EdFadeIn
#include "editmapscript.hpp"
#include "btactstatus.hpp" // coop-cutsceneview: whether the scene draws its player
#include "npcharacter.hpp" // coop-mayorhouse: the room's villagers drawn
#include "menu_draw.hpp" // coop-floorgate: MENU_SOUND_*
#include "menu_save.hpp" // coop-apartsync: GetAddAttachItem, as _SITEM_GET
#include "snd.hpp"       // coop-floorgate: SndSePlay
#include "savedata.hpp"
#include "stockitem.hpp"
#include "framevu1.hpp"
#include "coop/coop.hpp"
#include "coop/coop_core.hpp"
#include "coop/coop_audio.hpp"  // coop-floorgate: the gathered question's sounds, the asked player's
#include "coop/coop_camera.hpp" // coop-dungeonjoin: her camera as she becomes the partner
#include "coop/coop_georama.hpp" // coop-stage4 4d
#include "coop/coop_townmenu.hpp" // coop-mergecam: the pause-at-split test hook
#include "coop/coop_combat.hpp" // StarterWeapon
#include "coop/coop_fullmenu.hpp"
#include "coop/coop_inventory.hpp" // coop-dungeonp2: the bags apart
#include "coop/coop_party.hpp" // coop-dungeonjoin: her member loaded as the partner
#include "coop/coop_actor.hpp" // pt1111c: kMaxPlayers
#include "coop/coop_interact.hpp" // coop-keyring: KeyRingTest's lines
#include "coop/coop_menu.hpp"     // coop-p2prompt: MenuNotePrompt
#include "coop/coop_prompt.hpp"
#include "coop/coop_spawn.hpp"
#include "coop/coop_town.hpp" // coop-p2entry: her member
#include "coop/coop_town.hpp"
#include "coop/netplay.hpp"
#include "coop/netplay_socket.hpp"
#include "coop/netplay_test.hpp"
#include "coop/coop_storage.hpp" // coop-small2: the party storage's asks apart
#include "dun/gameloop.hpp"
#include "app/gameloop.hpp"
#include "gfx/gfx.hpp"
#include "platform/paths.hpp"
#include "main.hpp"
#include "mainselect.hpp"
#include "platform/config.hpp"
#include "platform/input.hpp"
#include "userstatus.hpp"
#include "runscript.hpp"
#include "dataread.hpp"      // coop-doorgate: the rooms' scripts read at the table's scan
#include "data/data_read.hpp"
#include "gamepad.hpp"         // coop-gateflow: the floor menu's pad modes off at its Circle
#include "quest/host.hpp"       // coop-pt1111e: QuestHostSetup (the quests registered before their progress is read)
#include "quest/framework.hpp"  // coop-pt1111e: the quests' progress (SaveJson, LoadJson), a slot apart

namespace dc::game {
extern int  EdInteriorFlag;  // editloop
extern char EdInteriorName[0x20];
extern CCharacter *Chara; // editloop: the town's player
void CoopTownGeoramaToSave();                     // editloop
void CoopTownGeoramaPlotFromSave(int town, int plot); // editloop
void EditPartsObjectOnOff();                          // editloop: each plot's map objects shown as its placed count says

void EdGetFadeColor(float *colour); // editloop3: the editor fade (the fade test hook)

extern int maxFloorTbl__3[7]; // monstorunit: each dungeon's floors (coop-floorgate: its last is the boss's)

extern s32        mode;
extern s32        MapNo;
extern s32        gameTask;
extern CFrameVu1 *CharaFrame;

bool SaveSlotWriteNumbered(int file_no, std::vector<unsigned char> *written); // memorycardaccess.cpp (coop-dungeonp2: the save test)
unsigned int PortRandState();                // runtime.cpp (coop-flagsync: StepWater)
void         PortRandSetState(unsigned int state);
void         BtAtraListMake(int dungeon); // dungeonparts.cpp (coop-joinatla)
} // namespace dc::game

void PickJoinMerged(); // coop-cutscenejoin (below)
static bool g_prelink_drawn = false; // coop-localrace (CoopWorldsPrelinkSaid, below)
static bool g_prelink_spot = false;
namespace dc::game {
int  EdInSceneEvent(); // edit_in.cpp (coop-scenejoin)
} // namespace dc::game

extern bool g_become_due; // coop-p2entry (below): her member at the floor's next tick of play, apart
namespace {

constexpr std::uint32_t kMagic = 0x44435757; // "WWCD"

enum Kind : std::uint32_t {
    kPresence = 1, // where this copy is, twice a second while apart
    kProgress = 2, // a player's progress while apart, to the other copy (resent until acknowledged), both ways
    kProgressAck = 3,
    kSceneView = 4, // coop-cutsceneview: the sender's view of the floor's opening scene it plays, each tick (SceneView)
    kCharaNow = 5,  // coop-apartstats: the sender's characters' life, water and ailments now, with each presence (statsync::NowMsg)
};

// What a progress message carries.
enum What : std::int32_t {
    kWhatAtla = 1,  // an atla collected: a atra, b atra_no, c dungeon, d floor (getAtraToSaveData's arguments)
    kWhatFloor = 2, // a deeper floor reached: c dungeon, a floor
    kWhatSlot = 3,  // (a catch-up's recording only) player 2's live-sync record a, as the host applied it
    kWhatStand = 4, // (a catch-up's recording only) where player 2 stands at the merge (coop_spawn.hpp's SpawnPack)
    kWhatLoaded = 5, // coop-saveload: player 1 loaded a save from his menu while apart (a: the save's file number)
    kWhatFlag = 6,  // coop-flagsync: a game flag an event set: a flag, b value, c 0 a bit (_SSET_GAME_FLAG), 1 an int (_SSET_GAME_INT_FLAG)
    kWhatParty = 7, // coop-apartsync: the party grew (a character joined, _SSET_PARTY_NUM): a its size
    kWhatGate = 8,  // coop-gateopen: a floor's gate opened with its key: c dungeon, a floor (0 up)
    kWhatGain = 9,  // coop-apartstats: a character's lasting stat or a floor's kills went up or down: a statsync::Gain, b the
                    // character (kills: dungeon * 100 + floor), c by how much (water: a float's bits)
    kWhatNow = 10,  // coop-apartstats (a catch-up's recording only): a character's life, water and ailments the merge took from
                    // the other copy: a the character | part << 8, b c d that part's words (statsync::Pack)
    kWhatCall = 11, // coop-floorgate: the sender's gate to a floor that needs both players did not open: c dungeon, a that floor, d the
                    // sender's floor (0 up). A question for the other player (CallTick), nothing kept
    kWhatCallAnswer = 13, // coop-floorgate: the answer to a call: b 1 coming, 0 not now, 2 busy (not asked); c dungeon, a the floor called to (0 up)
    kWhatDoorCall = 12, // coop-doorgate: the sender's door into a room whose story needs both players did not open: a the room's
                        // row in the door table (doorgate::Find), b 1 the cave door (a unused). A notice only, as kWhatCall
    kWhatCommand = 15, // coop-localmenu (a catch-up's recording only): a command (netplay_cmd.hpp's NetplayApplyRecordedCommand)
    kWhatDoorAnswer = 14, // coop-doorgate: the answer to that call (a, b as kWhatDoorCall; c 0 coming, 1 not now, 2 busy, 3 no answer). A note only
};

// coop-joinatla: the random state this copy laid each dungeon's Atla out from (CoopWorldsAtlaListMake), for kWhatFloor's b.
unsigned g_atla_rand[6] = {};
bool     g_atla_rand_set[6] = {};

#pragma pack(push, 1)
struct Progress {
    std::uint32_t magic;
    std::uint32_t player;
    std::uint32_t kind; // kProgress, or kProgressAck (id only)
    std::uint32_t id;   // the sender's own count, from 1; the host applies each id once
    std::int32_t  what;
    std::int32_t  a, b, c, d;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct Message {
    std::uint32_t magic;
    std::uint32_t player;
    std::uint32_t kind;
    std::uint32_t seq;
    std::int32_t  mode;
    std::int32_t  map;
    std::int32_t  floor;
    // Where this copy last heard the other is (heard 1), so a merge starts only once each knows the
    // other knows they are on the same map.
    std::int32_t  heard_mode;
    std::int32_t  heard_map;
    std::int32_t  heard_floor;
    std::uint32_t heard;
    // Whether the sender is ready to merge (MergeReady): a copy links only once both are, so the two start
    // the link within a presence of each other instead of one waiting out the other.
    std::uint32_t ready;
    // coop-spawn: where the sender's own character stands in a town (has_pos 0 elsewhere), so the merge keeps her there.
    std::uint32_t has_pos;
    float         pos[3];
    float         yaw;
    // coop-localfix (Jep's note H): on a floor, the random state the sender built it from (NetplayFloorBuildRand), so a copy
    // that follows onto that floor builds the same one.
    std::uint32_t floor_rand_set;
    std::uint32_t floor_rand;
    std::int32_t  floor_rand_map;   // the dungeon and floor (0 up) it built: a floor still "on the way" (its scene) counts
    std::int32_t  floor_rand_floor;
    std::uint32_t floor_hold; // coop-localfix: the sender holds a floor change at its entrance for a merge (a menu pick of its own does not)
    // coop-dungeonjoin: on a floor (has_pos 2, pos and yaw then the player's there), the member the sender plays.
    std::int32_t  chara;
    // coop-mayorhouse: the sender's player is in a talk or an event inside a building (BusyIndoors): no merge there till it ends.
    // (coop-scenejoin 2: in a scene, which merges; coop-buildarrive 3: building apart in the game's Georama or her menu apart,
    // BuildsApart: no merge till it closes, and the other copy arriving there is not held black meanwhile)
    std::uint32_t busy;
};
#pragma pack(pop)

// coop-cutsceneview: what the copy playing a floor's opening scene shows of it, each tick, so the other copy, its floor start
// held for that scene (CoopWorldsHoldFloorStart), draws the same floor from it instead of black (CoopWorldsSceneView).
#pragma pack(push, 1)
struct SceneView {
    std::uint32_t magic;
    std::uint32_t player;
    std::uint32_t kind; // kSceneView
    std::uint32_t seq;
    std::int32_t  map;   // the dungeon and floor (0 up) whose scene it is (the floor built, not started)
    std::int32_t  floor;
    float         eye[3]; // its camera's eye and look point (world places) and roll
    float         ref[3];
    float         roll;
    float         place[3]; // where its player is drawn (a scene's node may carry him: PlayerDrawnPlace) and its yaw
    float         yaw;
    std::int32_t  chara;  // the member it plays
    std::int32_t  motion; // the motion its player plays, and its flags
    std::int32_t  motion_flags;
    std::uint32_t drawn; // its player is drawn
};
#pragma pack(pop)

static_assert(sizeof(SceneView) != sizeof(Message), "worlds messages are told apart by their size");
static_assert(sizeof(SceneView) != kCoopWorldsScenePacksBytes && sizeof(Message) != kCoopWorldsScenePacksBytes, "worlds messages are told apart by their size");
static_assert(sizeof(Progress) != sizeof(Message), "worlds messages are told apart by their size");

// Where a copy is: the town (mode EDIT, its map) or the dungeon floor (mode DUNGEON, the dungeon and
// its floor); anything else (a load, the title) has map -1.
struct Zone {
    int mode = -1;
    int map = -1;
    int floor = -1;
};

bool operator==(const Zone &a, const Zone &b) {
    return a.mode == b.mode && a.map == b.map && a.floor == b.floor;
}

// coop-localfix2: the zone this copy is in and when it came into it.
struct Arrival {
    Zone                                  zone;
    std::chrono::steady_clock::time_point at{};
};
Arrival g_arrival;

struct State {
    bool         apart = false;
    bool         stayer = false;
    std::int64_t split_tick = -1;
    std::chrono::steady_clock::time_point split_at{}; // coop-natsplit: when, by the clock (a split, not a drop)
    // The copy that left a town by an exit, still in that town (its jump's fade and load to come): no merge till it is
    // out, or back to walking there a while (the exit did not happen).
    bool         leaving = false;
    Zone         left_from;
    int          leaving_walk = 0;
    bool         merged_stayer = false; // stayer as the last merge began (a merge that does not hold restores it)
    // coop-houseenter: this copy is in its zone since before the other's last change of zone (whoever moved last is the one
    // arriving, whoever stayed at the split or not); the zone it was in when that was settled; as the last merge began.
    bool         first_here = false;
    Zone         first_here_zone;
    bool         merged_first_here = false;
    // coop-loadpull: player 1 loaded a save apart (his copy: from his load; hers: from its word): the next merge is by his
    // state wherever she is, then until that merge has held
    bool         load_pull = false;
    bool         load_pull_said = false;
    // coop-heldscene: a merge by her state inside a room (armed until his first walk step there); the room's arrival event
    // started again after it, until it is over; the story flag it sets; the merge's tick.
    bool         arrival_again = false;
    Zone         arrival_again_zone;
    int          arrival_running = 0;
    int          arrival_held = 0; // this copy held its room's arrival (CoopWorldsHoldArrivalEvent), in that room
    Zone         arrival_held_zone;
    int          arrival_flag = -1;
    std::int64_t merge_tick = -1;
    bool         up = false;
    std::int64_t tick = -1;
    bool         exit_fired = false;
    int          apart_ticks = 0; // ticks of play since the split (the apart copy's test exit)
    int          merges = 0;

    // The other copy while apart: where it said it is, when it last said so, and the sequence of its last.
    Zone                                  peer;
    Zone                                  peer_sees; // where the other copy last heard this one is
    bool                                  peer_sees_set = false;
    bool                                  peer_heard = false;
    std::chrono::steady_clock::time_point peer_at{};
    std::uint32_t                         peer_seq = 0;
    bool                                  peer_ready = false; // the other copy's last presence said it is ready to merge
    bool                                  peer_busy = false;  // coop-mayorhouse: and that its player is in a talk indoors (a scene too with scene_merge_off)
    bool                                  peer_scene = false; // coop-scenejoin: or in a scene indoors (which a merge goes into)
    bool                                  peer_talk = false;  // coop-stage4 4e part 1: or in a talk indoors that a merge goes into (TalkHere)
    bool                                  peer_builds = false; // coop-buildarrive: or building apart (BuildsApart)
    bool                                  logged_build_wait = false; // coop-buildarrive
    bool                                  logged_scene_merge = false;
    bool                                  logged_town_scene_merge = false; // coop-exitscene
    bool                                  logged_town_busy_wait = false;   // coop-dunfreeze
    std::uint32_t                         sent_busy = 0; // (coop-scenejoin: the busy word sent, Message::busy)
    bool                                  logged_busy_wait = false;
    coop::SpawnStand                      peer_stand; // coop-spawn: where the other copy's character last stood in a town
    bool          peer_floor_rand_set = false; // coop-localfix: the other copy's floor's build state (Message::floor_rand)
    std::uint32_t peer_floor_rand = 0;
    int           peer_floor_rand_map = -1;
    int           peer_floor_rand_floor = -1;
    bool          peer_floor_hold = false; // coop-localfix (Message::floor_hold)
    // coop-dungeonjoin: where the other copy's player last stood on a floor (Message::has_pos 2), and its member.
    struct {
        bool  set = false;
        Zone  zone;
        float pos[3] = {};
        float yaw = 0.0f;
        int   chara = -1;
    } peer_floor_stand;
    bool                                  sent_ready = false;

    std::uint32_t                         seq = 0;
    std::chrono::steady_clock::time_point presence_due{};
    Zone                                  sent_here;
    std::chrono::steady_clock::time_point merged_at{};
    bool                                  merge_pending = false; // merged, not yet held for kMergeHoldMs
    bool                                  announced = false;
    Zone                                  logged_here;
    Zone                                  logged_peer;
    bool                                  logged_heard = false;
    std::chrono::steady_clock::time_point held_logged{};
    char                                  status[160] = "";
    // The story guard: the event it last held, when, and how long the HUD says why.
    int                                   held_event = -1;
    int                                   pickup_logged = -1; // coop-closet: the last pickup let through, logged once
    std::chrono::steady_clock::time_point held_at{};
    int                                   town_ticks = 0; // ticks of town walking since the split (the test event)

    // Progress made apart. Player 2's copy: what it sent and the host has not acknowledged (resent twice a
    // second), its next id and the deepest floors it has told of. The host: the ids it has applied.
    std::vector<Progress>                 progress_out;
    std::uint32_t                         progress_id = 0;
    std::chrono::steady_clock::time_point progress_due{};
    int                                   progress_dropped = 0; // the test's lost sends done
    signed char                           floors_told[7] = {};
    bool                                  floors_set = false;
    std::unordered_set<std::uint32_t>     progress_applied;
    // coop-catchuprace: player 2's copy: every progress it made, by id (a late one's lockstep command applies it again here);
    // the host: hers that reached it after its catch-up's last blob, till their lockstep commands.
    std::unordered_map<std::uint32_t, Progress> progress_sent;
    std::unordered_map<std::uint32_t, Progress> progress_late;
    bool                                  test_progress_held = false; // DC_WORLDS_TEST_FLAG_AT_MERGE: sends held to the last blob
    int                                   progress_ticks = 0; // ticks of floor play apart (the test atla)
    // coop-flagsync: the newest id applied for each flag (c * 0x10000 + flag), so an older resend that lands late does not
    // put an older value back; and whether a flag is being applied from the other copy (not sent back).
    std::unordered_map<std::int32_t, std::uint32_t> flag_applied;
    bool                                  flag_from_peer = false;
    int                                   flag_ticks = 0; // ticks of play apart (the test flag)
    // coop-apartsync: the party's size as this copy last told of it (or heard of it), so a character joining on one copy
    // apart joins on the other.
    int                                   party_told = 0;
    // coop-apartsync: apart because the link dropped while together (not a split), and since when this copy has heard a
    // Hello from the other with no word of its own apart play (a copy that came back new, not one that played on).
    bool                                  dropped = false;
    std::chrono::steady_clock::time_point fresh_wait_since{};
    bool                                  was_linked = false; // the lockstep ran on the tick before (NetplayPartnerLinked)
    int                                   apart_hook_ticks = 0; // ticks of play apart (the party and script item hooks)

    // Joining on the next floor: this copy has finished its floor (faded out) and waits there to merge with
    // the other, who is at this dungeon's entrance; since when.
    bool                                  floor_hold = false;
    std::chrono::steady_clock::time_point floor_hold_since{};
    std::int64_t                          floor_ticks = 0; // ticks of floor play apart (the test's floor change)
    int                                   hold_given_up_map = -1; // the floor whose gate no longer waits
    int                                   hold_given_up_floor = -1;
};

State g_state;
// Test hook latch (DC_WORLDS_TEST_PICK_ON_PEER_WAYIN): the other copy heard at a dungeon's entrance, its splash (-2) or floor menu (-1).
struct TestPeerEntrance {
    int  map = -99;
    bool splash = false;
    bool menu = false;
} g_test_peer_entrance;

long long g_merge_face_at = -1; // coop-uiflash: the link tick of the merge's facing line (DC_COOP_DEBUG)
std::int64_t g_merging_split = -1000; // coop-twinfix: the split (its tick) whose merge this copy has started ("merging ..."), for TestApartPresses
bool  g_test_merge_started = false; // this copy has started a merge's link (CoopWorldsMayLink), for DC_WORLDS_TEST_SLOT_AT_MERGE

// coop-worlds: live sync while apart (below).
namespace slotsync {
void Begin();
bool Idle();
void SwapBags(bool want); // coop-dungeonp2
void BagDump(const char *label); // coop-mergebags
void Flush();                    // coop-apartstats
void Settle(int town);
} // namespace slotsync

// coop-apartstats: each character's stats and the floors' kills apart (below).
namespace statsync {
void Begin();
void Rebase();
void Loaded();
void Tick();
void Send();
bool Receive(const void *data, int size);
bool PeerHeard();
void Merge();
const char *Apply(const std::int32_t delta[5]);
void Dump(const char *label);
} // namespace statsync

// On for every linked pair (game.netplay 1 or 2: the Linked and Online launchers); DC_COOP_WORLDS=0 turns it
// off. It does nothing unless the link is up or the copies split (single-process Co-op never is).
bool On() {
    static const bool on = !TestEnvIs("DC_COOP_WORLDS", "0");

    return on;
}

int Player();

// coop-dungeonp2: player 2's copy apart, with separate bags, plays from her bag; together it is back.
bool Mutated(const char *feature);

const char *FloorName(int map, int to); // coop-floorgate (below)
void        CallReceived(int from, int map, int to, int floor);
void        CallAnswered(int map, int to, int answer);
void        CallTick();
void        CallWithdrawn(int from); // coop-gatepopup (below)
CDungeonEventData *GateEvent(int script_no); // (the door vote's, below)
void ApplyLateProgress(int player, const NetCommand &command, std::int64_t tick); // coop-catchuprace (below TakeProgress)

// coop-doorgate: the other player's call from a door that did not open, as this copy shows it (below).
namespace doorgate {
const char *CallText(int player, int row, bool cave);
void        Scan();
void        Withdrawn(int asker); // coop-gatepopup
void        AskApart(int asker, int row, bool cave);
void        Answered(int asked, int row, bool cave, int answer);
void        Close(int answer);
} // namespace doorgate

// coop-mergebags: a catch-up merge under way on this copy, from the host's checkpoint to the lockstep (its waits between
// blobs too). The copy is still apart then, but it steps the host's recording from an older state: its shared slots are
// not its own play. DC_WORLDS_MUTATE=scan_in_catchup (twin): only the replay's own ticks count, as before.
bool CatchupUnderWay() {
    return Mutated("scan_in_catchup") ? NetplayCatchingUp() : NetplayCatchUnderWay();
}

void FollowApart() {
    // Only while no link is up or catching up: a merge under way (its first try may drop, a catch-up replays) must
    // step both copies from the same bags, and a save on the way (CoopSaveBagsHome) must not swap them back in.
    // coop-housemerge: through the whole catch-up, its waits between blobs too: a wait swapped her bag in, and the next
    // replay tick played the host's story with it (the mayor's gifts went into her bag, house-after 2026-10-25).
    // DC_WORLDS_MUTATE=swap_in_catchup (twin): only the replay's own ticks keep the bags home, as before.
    const bool catching = Mutated("swap_in_catchup") ? NetplayCatchingUp() : NetplayCatchUnderWay();

    slotsync::SwapBags(On() && g_state.apart && !g_state.up && !catching && Player() == 2 && coop::OwnBags());
}

// A merge whose link goes down this soon did not hold (one copy linked, the other did not): apart again.
constexpr int kMergeHoldMs = 15000;

// The other player's character this near the one who opens a door (units): both go in together.
constexpr float kDoorTogether = 40.0f;

// coop-pt1109b: DC_COOP_MUTATE=feature, read here (the file's own Mutated below is DC_WORLDS_MUTATE's).
static bool CoopMutateHas(const char *feature) {
    const char *value = TestEnv("DC_COOP_MUTATE");

    return value != nullptr && std::strstr(value, feature) != nullptr;
}

// DC_WORLDS_MUTATE=feature breaks that feature for a test's twin.
bool Mutated(const char *feature) {
    const char *value = TestEnv("DC_WORLDS_MUTATE");

    return value != nullptr && std::strstr(value, feature) != nullptr;
}

// This copy's player in the link (1 or 2), up or not: game.netplay, or DC_NETPLAY.
int Player() {
    return NetplayRole(); // game.netplay or DC_NETPLAY (one client: as it hosts or joins)
}

// A building's number in a town's zone (its floor field, -1 outdoors): from its name ("i01h06"), the same on
// both copies.
int InteriorId(const char *name) {
    unsigned id = 0;

    for (const char *c = name; c != nullptr && *c != '\0'; c++) {
        id = id * 31u + (unsigned char) *c;
    }

    return (int) (id % 9000u) + 1;
}

// coop-inbuild: this copy split by going out of a building alone and its door is still opening (it is outside already:
// a merge with the one inside would pull it back in).
bool g_leaving_building = false;

// coop-floorsnap: the floor whose "go dungeon" ran last (selectMapNo * 256 + floor), -1000 none (CoopWorldsFloorStarted).
int g_floor_started_key = -1000;
int g_floor_started_prev = -1; // coop-floorunlock: and its prev_floor then

// coop-localfix: this copy's floor has had a tick of play since it started (its opening scene over).
bool g_floor_played = true;

// coop-localfix: a merge's host state (a resync's, or a snapshot's) is in this copy while it is still apart: from here its
// game's player is player 1's (and hers the partner), until the merge ends or the pair splits again.
bool g_merge_state_in = false;

Zone Here() {
    Zone zone;

    zone.mode = mode;

    if (mode == GAME_MODE_EDIT) {
        zone.map = MapNo;

        if (EdInteriorFlag == 0) {
            g_leaving_building = false;
        }

        // coop-worlds: inside a building (or its door opening) is another place than the town outside.
        // DC_WORLDS_MUTATE=leaving_inside (twin): the leaver is still inside while its door opens, as before.
        // coop-natsplitflake (train 48, mp-nat-split~twin under load): and the map jump its door hands over to (the town's
        // loop sets ED_MODE_MAP_JUMP and loads the room in that tick; the interior flag comes on with the room's first tick).
        // That tick said "the town", and under load it lasted long enough for her copy to hear him outside and link alone.
        // DC_WORLDS_MUTATE=jump_place_town (twin): the town in that tick, as before.
        const bool door_jump = GameMode == ED_MODE_MAP_JUMP && !Mutated("jump_place_town");

        if ((EdInteriorFlag != 0 || GameMode == ED_MODE_DOOR_OPEN || door_jump) && (!g_leaving_building || Mutated("leaving_inside"))) {
            zone.floor = InteriorId(EdInteriorName);
        }
    } else if (mode == GAME_MODE_DUNGEON && UserStatus != nullptr) {
        zone.map = selectMapNo;
        // At the entrance's floor menu, or holding a floor change for a merge there: the entrance (floor -1). coop-floorsnap:
        // a floor not started yet (its gate's number set, its "go dungeon" to come): on the way (-2).
        // coop-startjoin (Jep's 2026-10-27 run: P2 back on B1 after an escape, P1 coming onto it just after her; she fell in
        // a black void and his copy crashed): the last floor's start is spent at the floor menu, else a floor picked again
        // (B1, cur_floor 0 as each entry starts) counts as started from its pick, under its arrival, with her player still at
        // (0 0 0) and her floor not played: the merge went by that state. DC_WORLDS_MUTATE=stale_floor_start (twin): kept.
        if (BtGameModeFlag == BT_GAME_MODE_ENTRANCE_MENU && !Mutated("stale_floor_start")) {
            g_floor_started_key = -1000;
        }
        const bool started = g_floor_started_key == selectMapNo * 256 + UserStatus->cur_floor || Mutated("floor_early");
        // coop-cutscenejoin: or this copy's own arrival on the floor the other plays, under way (CoopWorldsPickJoining).
        // coop-b1lesson: or a join in the floor's opening scene (CoopWorldsSceneJoining), on both copies.
        zone.floor = BtGameModeFlag == BT_GAME_MODE_ENTRANCE_MENU || g_state.floor_hold ? -1
                     : started || CoopWorldsPickJoining() || CoopWorldsSceneJoining() != 0 ? UserStatus->cur_floor
                                                                                          : -2;
    }

    return zone;
}

// coop-houseenter (Jep, 2026-10-27: "P2 being in the mayor's house and then P1 entering caused P2 to get a black splash
// screen and wait a few seconds and then become invisible"): whether this copy was in its zone before the other came, so a
// merge here goes by its state. Whoever moved last arrives: she went in first and he followed, so she was there first,
// though he stayed outside at the split. DC_WORLDS_MUTATE=stayer_outdoors (twin): the one that stayed at the split, in a
// town's outside only, as before (his state went over hers in the house: her copy reloaded the town and came in by his door).
bool FirstHere() {
    if (Mutated("stayer_outdoors")) {
        return g_state.stayer && EdInteriorFlag == 0;
    }
    return g_state.first_here && Here() == g_state.first_here_zone;
}

// Where a NextMapNo goes: a town (below 200) or a dungeon's floor select (200 on, floor unknown).
Zone Target(int next_map) {
    Zone zone;

    if (next_map >= 0 && next_map < 200) {
        zone.mode = GAME_MODE_EDIT;
        zone.map = next_map;
    } else if (next_map >= 200 && next_map < 300) {
        zone.mode = GAME_MODE_DUNGEON;
        zone.map = next_map - 200;
    }

    return zone;
}

const char *DungeonName(int map) {
    static const char *const names[7] = {"Divine Beast Cave", "Wise Owl Forest", "Shipwreck", "Sun and Moon Temple",
                                         "Moon Sea",          "Gallery of Time", "Demon Shaft"};

    return map >= 0 && map < 7 ? names[map] : "a dungeon";
}

void Describe(const Zone &zone, char *out, std::size_t size) {
    if (zone.mode == GAME_MODE_EDIT && zone.map >= 0 && zone.floor > 0) {
        std::snprintf(out, size, "in %s, inside building %d", zone.map == 0 ? "Norune" : zone.map == 1 ? "Matataki" : "town", zone.floor);
    } else if (zone.mode == GAME_MODE_EDIT && zone.map == 0) {
        std::snprintf(out, size, "in Norune");
    } else if (zone.mode == GAME_MODE_EDIT && zone.map == 1) {
        std::snprintf(out, size, "in Matataki");
    } else if (zone.mode == GAME_MODE_EDIT && zone.map >= 0) {
        std::snprintf(out, size, "in town (map %d)", zone.map);
    } else if (zone.mode == GAME_MODE_DUNGEON && zone.map >= 0 && zone.floor >= 0) {
        std::snprintf(out, size, "in %s, floor %d", DungeonName(zone.map), zone.floor + 1);
    } else if (zone.mode == GAME_MODE_DUNGEON && zone.map >= 0) {
        std::snprintf(out, size, "at %s's entrance", DungeonName(zone.map));
    } else {
        std::snprintf(out, size, "on the way");
    }
}

void Log(const char *line) {
    std::fprintf(stderr, "worlds: %s\n", line);
    std::fflush(stderr);
}

// Whether this copy is ready to merge: apart, in a town, and it has heard that the other copy is in the same
// town and knows this one is there too.
// This copy's player in the game's Georama apart (its editor, parts menu or the pause over it), in a town outdoors.
// coop-twinfix (the bags-merge thread's open item, "her own menu open at the merge is not carried"; w-town-apart -Case doorout
// -ApartMenuOpen): player 2's copy apart in town has the game's own menu open (hers: apart she is its player). A merge by her
// state took it ("the state set is taken with the menu open" after 3 s) and the game's menu is player 1's on both copies once
// linked: Toan stood in a menu he never opened, hers gone from her. As her Georama (ApartBuilds), it merges as she closes it.
bool ApartHerMenu() {
    return Player() == 2 && EdInteriorFlag == 0 && (GameMode == ED_MODE_MENU_INIT || GameMode == ED_MODE_MENU);
}

bool ApartBuilds() {
    return EdInteriorFlag == 0 && (GameMode == ED_MODE_GEORAMA || GameMode == ED_MODE_GEORAMA_MENU_INIT || GameMode == ED_MODE_GEORAMA_MENU ||
                                   GameMode == ED_MODE_RETURN_MENU_GEORAMA);
}

// coop-dungeonsuite (x-dungeon-descent -Case ahead: player 1 said "leave anyway", and the pair merged again on the floor he
// was leaving at link tick 34, his gate undone): the copy that leaves a floor anyway (FloorSplit) still stands on it while the
// gate's script runs, so both copies said the same floor and merged there. It is not ready until it has left that floor
// (map * 256 + floor; -1 when none). DC_WORLDS_MUTATE=gate_merge_back (twin): ready at once, as before.
int g_floor_leaving = -1;

// coop-mayorhouse (Jep's 2026-10-27 playtest: P2 into the mayor's house while P1 talked to the mayor, or in his first-visit
// scene, sat on black till he was done, 19.6 and 47 s): this copy's player in a talk or an event inside a building. A merge
// waits on a quiet frame of the state set's sender (a running script lives on the heap), so it is not ready to merge while
// it is busy, and the other copy, which knows it from its presence, plays the room on its own meanwhile (not held on
// black: CoopWorldsArrivedWhereStayer), seeing none of it. They merge once it is over.
// (coop-stage4 4e part 1: his talk no longer holds a merge, TalkHoldsMerge; merge_while_busy retired with l-local-play-busytalk.)
// coop-scenejoin (Jep, 2026-10-27: "if P2 joins an area where P1 is in a cutscene, P2 must view that cutscene, and anything
// it does to P1 must happen to P2 too"): a scene (an event, room mode 4) no longer keeps a merge off. The state set carries a
// script under way (the VM's stacks and records live in its pinned arena, runscript.cpp), so the copy in the scene sends its
// state mid-scene and the arriving copy plays the rest of it in lockstep: the same scene on both screens, and what it does
// (flags, items, a jump to another place) done on both. A talk still waits (talks stay Toan's: her copy plays the room).
// DC_WORLDS_MUTATE=scene_merge_off (twin): a scene waits as a talk does, as before.
bool SceneIndoors() {
    return mode == GAME_MODE_EDIT && EdInteriorFlag != 0 && CoopTownIndoorMode() == 4; // ED_IN_MODE_EVENT
}

// coop-exitscene (Jep's 2026-10-27 run: P1 back from the dungeon played the town's scene; P2 out of the dungeon after him "got
// a black screen instead of joining that cutscene"): a town's event outdoors too (the town's mode). His copy waited for a
// quiet frame to send its state (SnapshotQuietTick), her screen black all through his scene (96 s in that run). The event's
// script is in the set as a room's is (the VM's pinned arena), and the town's event state is the game's globals, so the
// merge goes into it as indoors: both play the rest of it, her screen on its camera (OwnTownView draws the game's while the
// town is not walking), whatever it does done on both.
// (coop-pt1106-batch: not one player's own town event, Norune's fishing board: that is its reader's alone, CoopTownOwnTownEvent)
bool SceneOutdoors() {
    return mode == GAME_MODE_EDIT && EdInteriorFlag == 0 && GameMode == ED_MODE_EVENT && CoopTownOwnTownEvent() == 0;
}

bool SceneHere() {
    return SceneIndoors() || SceneOutdoors();
}

// coop-closetnet (Jep's 2026-10-28 Steam run: "Xiao opened the closet, Toan got warped to it and opened it instead"): on
// player 2's copy apart, a room's event is her own (story events are held for her apart, HoldStory: what runs is a pickup,
// the closet's 12, or an arrival as seen), with the game's player, her, its main character. A merge into it (coop-scenejoin)
// made her the partner and put Toan in the game's player (CoopTownBecomePartner), so the rest of her event played on him,
// on both copies: his walk to the closet, its item. Her event is hers alone, so it is a busy room as a talk is: no merge till
// it ends, the arriver playing the room meanwhile. The Steam run's 110 s catch-up only widened the window: in Local the
// same press as he walks in does it. DC_WORLDS_MUTATE=merge_into_her_event (twin): merges into it, as before.
// coop-stage4 4e part 2 (Jep 11-07: no exceptions): no hold any more: the merge comes at once, her skin stays the game's player
// through her event and the swap runs at its end on every copy (coop_town's SwapDefer). DC_WORLDS_MUTATE=event_holds_merge (twin): the hold, as before.
bool HerEventApart(int room_mode) {
    return room_mode == 4 && Player() == 2 && g_state.apart && !NetplayStayerMerge() && Mutated("event_holds_merge");
}

// coop-stage4 4e part 1 (Jep 11-07, merge-no-wait: talks land at once): a talk on this copy (room mode 3) holds a merge only
// when it is hers apart (her skin the game's player there, as HerEventApart; 4e part 2). His talk, or any talk on player 1's
// copy, merges at once: its state (the message window, the villager, the script in the VM's pinned arena) is the game's, in the
// set (CoopWorldsTalkTakeOk), and the arriver plays the rest of it in lockstep, his window out of sight on her screen as a
// together talk. DC_WORLDS_MUTATE=talk_holds_merge (twin): every talk holds the merge, as before.
bool TalkHoldsMerge(int room_mode) {
    return room_mode == 3 && (Mutated("talk_holds_merge") || (Player() == 2 && g_state.apart && !NetplayStayerMerge() && Mutated("event_holds_merge")));
}

bool BusyIndoors() {
    const int room_mode = mode == GAME_MODE_EDIT && EdInteriorFlag != 0 ? CoopTownIndoorMode() : -1;

    return TalkHoldsMerge(room_mode) || (room_mode == 4 && Mutated("scene_merge_off")) || HerEventApart(room_mode); // ED_IN_MODE_TALK, ED_IN_MODE_EVENT
}

// coop-dunfreeze (Jep's 2026-10-28 Steam run: she took the cave door apart as his copy came back to walking in Norune; his
// merge went into her event, his copy linked to take her state and sat on black 64 s, the world map read his pad, not hers,
// and she left for the cave mid-join): outdoors as indoors (HerEventApart), her own town event apart is hers alone. Her copy
// never sends its state mid-event outdoors (CoopWorldsTownSceneTakeOk is player 1's: her pad holds the game's then), and
// the event may take her out of town. So it is a busy town: no merge till it ends, his copy plays on in the town meanwhile
// (not held black: peer_busy). DC_WORLDS_MUTATE=merge_into_her_town_event (twin): merges into it, as before.
// coop-pt1109c (Jep's 11-09 playtest 1s: she cancelled her floor menu's call, came back to Norune as P1 took the dungeon door apart; "she watched his dungeon scene and ended up
// outside"): a town event whose only way out is into a dungeon runs on its starter's copy alone together (CoopWorldsDungeonEventAlone splits the pair as it starts). Apart the same
// event counted as a scene to merge into (SceneOutdoors), so she was taken into it, both screens on its camera, placed, and split again at its jump. It is his alone there as well:
// no merge into it, a busy town till it leaves, she walks on where she is (peer_busy: never held). Either player. DC_WORLDS_MUTATE=merge_into_door_event (twin): as a scene, as before.
// coop-pt1109f (Jep 11-09, his answer A): final, for both players: the arriver does not merge into it, does not see it, and walks on. His 11-07 "a scene that leaves town takes every
// watcher" is for real story scenes (CoopWorldsStorySceneLeaves). What IntoDungeonOnly reads is a door in the game's own data: of every town's events with a _MAP_JUMP (a listing of
// maps 0, 2, 3, 4), all it takes are entrances; the one that writes a flag is Norune's 130, the door's own flag 1. The other scene with a jump, Matataki's 12, also goes into a building.
// Test hook DC_WORLDS_TEST_DOOR_AS_SCENE=1 (w-town-cavereach-herscene, x-town-stayermenu-hisscene): 130 stands in for a story scene that leaves town: no fixture reaches 12's jump.
bool DoorAsSceneForTest() {
    static const bool as_scene = TestEnvOn("DC_WORLDS_TEST_DOOR_AS_SCENE");

    return as_scene;
}

bool DoorEventApart() {
    return mode == GAME_MODE_EDIT && EdInteriorFlag == 0 && GameMode == ED_MODE_EVENT && g_state.apart && !Mutated("merge_into_door_event") && !DoorAsSceneForTest() && EdEventData != nullptr &&
           EdEventInfo.event_no > 0 && ::CoopWorldsIntoDungeonOnly(EdEventData, EdEventInfo.event_no);
}

bool BusyOutdoors() {
    if (DoorEventApart()) {
        return true;
    }
    return mode == GAME_MODE_EDIT && EdInteriorFlag == 0 && GameMode == ED_MODE_EVENT && Player() == 2 && g_state.apart && !NetplayStayerMerge() &&
           Mutated("event_holds_merge");
}

bool BusyHere() {
    return BusyIndoors() || BusyOutdoors();
}

// coop-stage4 4e part 1: a talk in a room that does not hold the merge (TalkHoldsMerge): said in the presence (busy word 4), so
// tests can time on it (the peer-busy input mark, DC_WORLDS_TEST_GATE_APART_ON_BUSY) as on the old hold's word.
bool TalkHere() {
    return mode == GAME_MODE_EDIT && EdInteriorFlag != 0 && CoopTownIndoorMode() == 3 && !BusyIndoors();
}

// coop-buildarrive (Jep's 2026-11-04 pack 8 run: P1 out of a house while P2 built in the Georama apart: "P1 got a black screen,
// then could move but in a separate instance, then ~10 s later was warped into P2's instance"): a copy building apart, or
// with her menu open apart, is not ready to merge (coop-uiflash, coop-twinfix) and says so in its presence's busy word (3).
// The arriver's 8 s hold for a merge (CoopWorldsArrivedWhereStayer) read only "not busy", so his screen sat black 8 s for a
// merge that could not come, then he walked the town alone till she closed it. Now he is not held: he walks the town at once.
// DC_WORLDS_MUTATE=arrival_held_for_build (twin): not said, so the arriver is held black as before.
bool BuildsApart() {
    return mode == GAME_MODE_EDIT && g_state.apart && ((ApartBuilds() && !CoopGeoramaCarries()) || (ApartHerMenu() && !CoopTownMenuCarries())) && !Mutated("arrival_held_for_build");
}

// coop-pt1106-lockup test hook (DC_WORLDS_TEST_MERGE_IN_MENU=N): player 1 opens his town menu on walking outdoors apart, and his
// copy is ready to merge only with it open (Jep's run: the menu opened as a merge was held off by her event); N ticks into
// the catch-up's wait to walk, and each second after, his own pad presses Circle (TestMergeInMenu). The first pad is the
// hook's throughout, as DC_WORLDS_TEST_APART_PRESS's.
// coop-stage4 4c: DC_WORLDS_TEST_MERGE_IN_MENU_P2=1: the same on player 2's copy, her Triangle by the game's pad read (x-town-stayermenu -HerArrives).
// =build (4d): her Georama, by her Select. DC_WORLDS_TEST_MERGE_IN_MENU_BUILD=1: his, N as the first, by his Select (menumerge's build case).
static bool MergeHeldForTestMenu() {
    static const bool on = TestEnv("DC_WORLDS_TEST_MERGE_IN_MENU") != nullptr;
    static const char *p2_spec = TestEnv("DC_WORLDS_TEST_MERGE_IN_MENU_P2");
    static const bool  her = p2_spec != nullptr;
    static const bool  build = TestEnv("DC_WORLDS_TEST_MERGE_IN_MENU_BUILD") != nullptr || (her && std::strncmp(p2_spec, "build", 5) == 0); // (=buildpart too)

    if (!((on && Player() == 1) || (her && Player() == 2))) {
        return false;
    }
    if (her && std::strcmp(p2_spec, "talk") == 0) {
        return !(EdInteriorFlag == 0 && GameMode == ED_MODE_TALK); // (coop-stage4 4e part 2: held till her talk runs)
    }
    return build ? !ApartBuilds() : !(EdInteriorFlag == 0 && (GameMode == ED_MODE_MENU_INIT || GameMode == ED_MODE_MENU));
}

bool MergeReady() {
    Zone here = Here();

    if (MergeHeldForTestMenu()) {
        return false;
    }

    // coop-loadpull (Jep, 2026-11-02: "loading a save is a special case": everyone is resynced to player 1's loaded state, in
    // his zone): after his load apart both copies are ready wherever each is, his once her copy has his load's word
    // (acknowledged) and he walks in his loaded town, hers at once. The merge's link then goes by his state (netplay's Begin,
    // CoopWorldsLoadPull) and her copy takes it where his is. DC_WORLDS_MUTATE=load_no_pull (twin): the usual merge, by
    // whoever was there first (hers when he loaded into her zone: his load undone).
    if (g_state.load_pull && !Mutated("load_no_pull")) {
        // (not on a way out under way, either copy: a link then left with the old zone's exit, OUT OF SYNC, as MergeReady's below)
        return g_state.apart && g_state.peer_heard && !g_state.leaving &&
               (Player() == 2 || (g_state.progress_out.empty() && (mode != GAME_MODE_EDIT || GameMode == ED_MODE_WALK)));
    }

    if (g_floor_leaving >= 0) {
        if (mode == GAME_MODE_DUNGEON && UserStatus != nullptr && selectMapNo * 256 + UserStatus->cur_floor == g_floor_leaving) {
            return false;
        }

        g_floor_leaving = -1;
    }

    // Player 2's progress all acknowledged first: the merge then starts from a host state that has it. Not while this
    // copy is still on its way out of the town it left (Jep's 2026-10-24 log: a catch-up merge in Norune while player 1's
    // jump to the cave was under way, then his copy left with the pair together, OUT OF SYNC).
    // coop-floorsnap: both on one floor (floor >= 0) merges too, by the host's snapshot (CoopWorldsFloorJoin);
    // DC_WORLDS_MUTATE=no_floor_join (twin) keeps them apart there, as before.
    return g_state.apart && !g_state.leaving && g_state.progress_out.empty() && slotsync::Idle() && g_state.peer_heard &&
           (here.mode == GAME_MODE_EDIT ||
            // coop-localfix (Jep's 2026-10-26 run: "P2 got locked into a black screen while P1 selected a level and then got entered into
            // P1s level at the same time instead of just being able to select B1 after P1"): not at the entrance's floor menu, where
            // each picks a floor alone (the second builds the first's and they merge on it); a held floor change still merges there.
            // DC_WORLDS_MUTATE=entrance_merge (twin): at the floor menu too, as before.
            (here.mode == GAME_MODE_DUNGEON && here.floor == -1 && !Mutated("no_floor_merge") &&
             (g_state.floor_hold || g_state.peer_floor_hold || BtGameModeFlag != BT_GAME_MODE_ENTRANCE_MENU || Mutated("entrance_merge"))) ||
            (here.mode == GAME_MODE_DUNGEON && here.floor >= 0 && !Mutated("no_floor_join") && NetplaySnapshotAvailable() &&
             // coop-localfix (path block 2026-10-26, o-opening-resync -Step dungeon-before: her floor's opening scene still
             // ran as he arrived, the join found her "not in its zone" and reloaded her floor): ready once this copy's floor
             // has played a tick (not "plays now": a knocked-out player alone on it waits on her change screen, and the merge is
             // what brings her partner). DC_WORLDS_MUTATE=floor_merge_in_scene (twin): during its opening too, as before.
             (((g_floor_played && (!Mutated("floor_merge_needs_play") || (gameTask == GAME_TASK_PLAY && BtEventMode == 0))) || Mutated("floor_merge_in_scene"))))) &&
           // coop-localfix: inside a building, once its room is up (its presence then carries where she stands, for the merge's
           // stand: TownMergedIndoors). DC_COOP_MUTATE=room_merge_beside (twin): from the door's opening on, as before.
           (here.mode != GAME_MODE_EDIT || here.floor <= 0 || EdInteriorFlag != 0 || coop::Mutated("room_merge_beside")) &&
           // coop-uiflash (Jep's Local-mode list: "a Georama bar flashes on P1's screen"): not while this copy's player builds
           // apart (the game's Georama, its parts menu or its pause): a merge by this copy's state put the town in Georama
           // mode on player 1's copy, his Georama that he never opened (and hers gone from her). It merges as she closes it.
           // DC_WORLDS_MUTATE=merge_in_georama (twin): at once, as before.
           (here.mode != GAME_MODE_EDIT || !ApartBuilds() || CoopGeoramaCarries() || Mutated("merge_in_georama")) && // (coop-stage4 4d: goes through it)
           // (coop-stage4 r4: merge_in_her_menu retired, no test turned it on since 4c: merge_menu_retail is the old way's twin)
           (here.mode != GAME_MODE_EDIT || !ApartHerMenu() || CoopTownMenuCarries()) && // (coop-stage4 4c: her battle menu goes through it)
           here == g_state.peer && g_state.peer_sees_set && g_state.peer_sees == here && !BusyHere() && !Mutated("no_merge");
}

void SendPresence() {
    // coop-stayerhello: a stayer bit changed since the last Hello (there first a moment ago) goes in a Hello ahead of this
    // presence, which may say ready: the other copy links on that, by the Hello it holds.
    NetplayHelloSaysStayer();
    Zone    here = Here();
    bool    ready = MergeReady();

    if (ready && !g_state.sent_ready) {
        NetplayTiming("this copy says it is ready to merge");
        NetplayHelloNow(); // coop-areacard: a Hello with the ready word, so the other links on it, not on its next timed one
    }
    Message message{kMagic,     (std::uint32_t) Player(), kPresence,          ++g_state.seq, here.mode, here.map, here.floor, g_state.peer.mode,
                    g_state.peer.map, g_state.peer.floor, g_state.peer_heard ? 1u : 0u, ready ? 1u : 0u};

    // coop-spawn: in a town, where this copy's own character stands (Toan's body, or hers drawn in his place). coop-localfix (Jep's
    // 2026-10-26: "If P1 is in the mayors house when p2 enters, P2 spawns relative to P1, somewhere nearby them instead of at the
    // door"): inside a building too (kIndoorStand on the map), so a merge there keeps her where she came in. DC_COOP_MUTATE=
    // room_merge_beside (twin): outdoors only, as before.
    if (here.mode == GAME_MODE_EDIT && (here.floor <= 0 || (EdInteriorFlag != 0 && !coop::Mutated("room_merge_beside"))) && Chara != nullptr &&
        Chara->frame != nullptr) {
        float at[4];
        float rotation[4];

        Chara->GetPosition(at);
        Chara->GetRotation(rotation);
        message.has_pos = 1;
        message.pos[0] = at[0];
        message.pos[1] = at[1];
        message.pos[2] = at[2];
        message.yaw = rotation[1];
    }

    message.floor_hold = g_state.floor_hold ? 1u : 0u;

    // coop-dungeonjoin: on a floor, where this copy's player stands and who it is (a merge by her state puts player 1's
    // member where his copy has him: CoopWorldsFloorBecomePartner).
    // (coop-cutscenejoin: not in its own arrival, still on its way down: the other then uses the floor's entrance.)
    if (here.mode == GAME_MODE_DUNGEON && here.floor >= 0 && UserStatus != nullptr && CharaFrame != nullptr && !CoopWorldsPickJoining()) {
        sceVu0FVECTOR rotation;

        CharaFrame->GetRotation(rotation);
        message.has_pos = 2;
        message.pos[0] = CharaMain.pos[0];
        message.pos[1] = CharaMain.pos[1];
        message.pos[2] = CharaMain.pos[2];
        message.yaw = rotation[1];
        message.chara = UserStatus->cur_chara;
    }

    if (int built_map = -1, built_floor = -1; here.mode == GAME_MODE_DUNGEON && NetplayFloorBuilt(&built_map, &built_floor)) {
        message.floor_rand_set = 1;
        message.floor_rand = NetplayFloorBuildRand();
        message.floor_rand_map = built_map;
        message.floor_rand_floor = built_floor;
    }

    message.busy = BusyHere() ? 1u : SceneHere() ? 2u : BuildsApart() ? 3u : TalkHere() ? 4u : 0u; // (coop-stage4 4e part 1: 4 in a talk, which merges)
    // coop-mayorhouse (coop-scenejoin: 2 in a scene, which merges; coop-exitscene: outdoors too; coop-dunfreeze: her town event apart is busy)

    NetSocketSend(&message, sizeof message);
    statsync::Send(); // coop-apartstats: this copy's characters now, for the merge
    g_state.sent_here = here;
    g_state.sent_ready = ready;
    g_state.sent_busy = message.busy;
}

// coop-spawn: the host, as a town merge starts: player 2 is to stand where her copy last said she stands (both copies
// alike: the joiner takes it from the catch-up's recording or the resync's state), and whoever arrived last moves if the
// two stand on one spot. Player 1 arrived last when his copy left at the split. DC_COOP_MUTATE=spawn_reset_stayer (twin):
// none, so she is put beside Toan as before.
void StandForMerge() {
    const int stand_map = Here().floor > 0 ? (MapNo | coop::kIndoorStand) : MapNo; // coop-localfix: a room's stand is its own

    if (Player() != 1 || Here().mode != GAME_MODE_EDIT || !g_state.peer_stand.set || g_state.peer_stand.map != stand_map ||
        coop::Mutated("spawn_reset_stayer") || coop::Mutated("spawn_no_stands")) {
        coop::SpawnClearStand();
        return;
    }

    coop::SpawnStand stand = g_state.peer_stand;
    std::int32_t     delta[5];

    stand.arriver = g_state.stayer ? 2 : 1;
    coop::SpawnPack(stand, kWhatStand, delta);
    coop::SpawnSetStand(coop::SpawnUnpack(delta), "the host, as the merge starts");
    NetplayCatchupDelta(delta); // a catch-up joiner's replay sets it on the same tick
}


void UpdateStatus() {
    char where[96];
    int  other = 3 - Player();

    if (!g_state.peer_heard) {
        std::snprintf(g_state.status, sizeof g_state.status, "Apart: player %d not heard from", other);
        return;
    }

    auto silent = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - g_state.peer_at).count();

    Describe(g_state.peer, where, sizeof where);
    std::snprintf(g_state.status, sizeof g_state.status, silent > 30 ? "Apart: player %d was %s" : "Apart: player %d is %s", other, where);
}

// The split, on both copies alike (the exit's tick): the link goes apart, each copy alone.
// Who took the town's exit now under way: 1 (Toan, player 1's walking) or 2 (player 2's partner, CoopWorldsTownEventBy);
// whether its event has run yet; who left at the last split.
int  g_town_starter = 1;
int  g_reenter_building = 0; // the building both go back into after a merge there (its InteriorId), else 0
bool g_reentering = false;   // that walk in under way: GotoInterior takes both, no split
int  g_door_yes_building = 0; // coop-gatepopup: the house both go into on a door call's yes (doorgate::DoorYes), the same way
bool g_town_starter_ran = false;
int  g_last_leaver = 1;
bool g_flags_local = false; // coop-floorscene2: CoopWorldsLocalFlags

// The last merge has held: its 15 s (kMergeHoldMs) have passed, or a split came on the pair's shared tick, which only a
// held link runs (each copy split by its own lockstep on that one tick). Its "did not hold" undo (CoopWorldsTick: the
// stayer and "there first" put back from before the merge) is off from here, and so is his load's pull.
void MergeHeld() {
    g_state.merge_pending = false;
    if (g_state.load_pull) {
        g_state.load_pull = false; // coop-loadpull: his load's merge has held
        Log("player 1's load: its merge has held");
    }
}

void SplitTo(const Zone &to, int next_map, bool stays, int leaver) {
    char where[96];
    char line[200];
    char moved[200] = "";

    coop::TownNotesEnd(); // coop-cavehold: a door's message waiting for its player's Cross ends with the pair

    // coop-floorunlock (Jep's 2026-10-28 Steam run: he took B1's gate back to town, she stayed on B1, later took its gate and
    // came out; his floor menu then offered B3): the gate's "back to town" runs _SET_FLOOR_LEVEL (the next floor) before its
    // _MAP_JUMP, on both linked copies, so the stayer stood on B1 with cur_floor already B2's. Her own gate then went one on from
    // that (B3, past B2's gather), and the deepest floor she told of was B3. The stayer's floor goes back to the one it stands
    // on, before anything here reads it (Here()); the deepest floor reached stays raised (the gate was cleared: B2 is open for
    // both). Here and not in _SET_FLOOR_LEVEL: every dungeon's gate program runs the window's "back to town" as
    // SET_FLOOR_LEVEL, MAP_JUMP, RET with no wait between (event.stb, all seven alike), so this split is on that same tick, and it
    // is the one place that knows who stays; nothing else that program writes reaches the stayer. DC_WORLDS_MUTATE=stay_floor_moved
    // (twin): left moved, as before.
    if (stays && mode == GAME_MODE_DUNGEON && UserStatus != nullptr && g_floor_started_key / 256 == selectMapNo &&
        UserStatus->cur_floor != g_floor_started_key % 256 && !Mutated("stay_floor_moved")) {
        std::snprintf(moved, sizeof moved, "staying on floor %d: the leaver's way out had moved this copy's floor to %d (prev %d); put back (prev %d)",
                      g_floor_started_key % 256 + 1, (int) UserStatus->cur_floor + 1, (int) UserStatus->prev_floor, g_floor_started_prev);
        UserStatus->cur_floor = (s8) (g_floor_started_key % 256);
        UserStatus->prev_floor = (s8) g_floor_started_prev;
    }

    Describe(to, where, sizeof where);
    // coop-pt1111j (Jep's 11-11 playtest, both logs: a door taken within 15 s of a merge ran "the merge did not hold" on the
    // next tick, so the stayer and "there first" this split sets were put back to the ones from before that merge: the one who
    // stayed outside was "the one that left" again, and player 1's next merge stand named the wrong arriver). A door's split
    // only comes on a held link, as a floor's leaving does (FloorSplit). DC_WORLDS_MUTATE=door_split_unheld (twin): as before.
    if (!Mutated("door_split_unheld")) {
        MergeHeld();
    }
    g_state.apart = true;
    g_state.dropped = false; // coop-apartsync
    g_merge_state_in = false; // coop-localfix
    FollowApart(); // coop-dungeonp2
    g_state.stayer = stays;
    g_state.first_here = stays; // coop-houseenter
    g_state.first_here_zone = Here();
    g_state.split_tick = g_state.tick;
    g_state.split_at = std::chrono::steady_clock::now(); // coop-natsplit
    // coop-localfix (Jep's 2026-10-26 Local playtest, his notes 5 and 6: "escape dungeon" black-screened the other player,
    // then pulled them along): a leaver off a floor too. Its copy still stands on the floor while the way out's effect
    // and fade run, so it told the stayer "floor 1" and the pair merged there by a resync that loaded both copies into
    // the dungeon's floor menu. DC_WORLDS_MUTATE=floor_leaver_merges (twin): towns only, as before.
    g_state.leaving = !stays && (mode == GAME_MODE_EDIT || (mode == GAME_MODE_DUNGEON && !Mutated("floor_leaver_merges"))) &&
                      !Mutated("merge_while_leaving");
    g_state.left_from = Here();
    g_state.leaving_walk = 0;
    g_state.peer = stays ? to : Here();
    g_state.peer_heard = false;
    g_state.peer_busy = false; // coop-mayorhouse
    g_state.peer_talk = false;
    g_state.peer_builds = false; // coop-buildarrive
    g_state.peer_sees_set = false;
    g_state.presence_due = std::chrono::steady_clock::now();
    g_last_leaver = leaver;
    coop::TownMergePlaceDrop(); // coop-exitscene
    if (next_map >= 0) {
        std::snprintf(line, sizeof line, "split at tick %lld: player %d leaves for map %d (%s); this copy (player %d) %s", (long long) g_state.tick,
                      leaver, next_map, where, Player(), stays ? "stays" : "leaves");
    } else if (to.floor == -1) { // coop-inbuild: out of a building into its town
        std::snprintf(line, sizeof line, "split at tick %lld: player %d goes out of the building (%s); this copy (player %d) %s", (long long) g_state.tick,
                      leaver, where, Player(), stays ? "stays inside" : "goes out");
    } else {
        std::snprintf(line, sizeof line, "split at tick %lld: player %d goes into a building (%s); this copy (player %d) %s", (long long) g_state.tick,
                      leaver, where, Player(), stays ? "stays outside" : "goes in");
    }
    Log(line);
    if (moved[0] != '\0') {
        Log(moved);
    }
    // coop-mergecam test hook (DC_COOP_TEST_PAUSE_AT_SPLIT=1, both copies): her town pause opens at the split, on the split's
    // tick alike, and holds apart until her merge ends it (the town changed). She stands where the merge places her through
    // its state set while her stick is held: the case the nightly hit when a title Start press landed in town.
    if (mode == GAME_MODE_EDIT && TestEnvOn("DC_COOP_TEST_PAUSE_AT_SPLIT")) {
        CoopP2TownPauseForTest();
    }
    NetplayGoApart(stays ? (leaver == 1 ? "player 1 left the map" : "player 2 left the map") : "this copy left the map", leaver);
    UpdateStatus();

    // The deepest floors as both copies have them now (the same tick): each tells the other of deeper ones (coop-flagsync:
    // in town from the save's dungeon status, UserStatus being the dungeon's own).
    if (CUserStatus *floors = UserStatus != nullptr ? UserStatus : SaveData != nullptr ? (CUserStatus *) SaveData->GetDngStatus() : nullptr) {
        std::memcpy(g_state.floors_told, floors->floor_reached, sizeof g_state.floors_told);
        g_state.floors_set = true;
        g_state.party_told = floors->party_size; // coop-apartsync
    }

    slotsync::Begin(); // coop-worlds: live sync from what both hold now
    slotsync::BagDump("split");

    // coop-pt1109a test hook (DC_WORLDS_TEST_STARTER_APART=1, x-town-stayermenu -WhoDoor): as her cave door under his Georama does, right after the split
    // on her copy (the fixture's door is a building's: a floor and his Georama are not what the row checks).
    if (static const bool starter = TestEnvOn("DC_WORLDS_TEST_STARTER_APART"); starter && Player() == 2) {
        CoopWorldsTownEventBy(2);
    }
}

// coop-apartsync: the link dropped while the pair was together (the network cut out, or the other copy stopped answering;
// not a player's Leave). Both copies play on alone, each in the zone they shared, so this is a split in place: what each
// does from here (her bag, flags, a party join, Georama) goes to the other as apart play does, and the relink is a merge.
// Before, the copies played on "together" with nothing synced and the relink's resync put the host's state over hers: her
// play since the drop was lost, and the resync reloaded the zone. DC_WORLDS_MUTATE=no_drop_apart (twin): as before.
void DropApart() {
    char where[96];
    char line[240];
    Zone here = Here();

    Describe(here, where, sizeof where);
    g_state.apart = true;
    g_state.dropped = true;
    g_state.fresh_wait_since = {};
    FollowApart(); // coop-dungeonp2: her bag in play on her copy
    g_state.stayer = true; // neither left
    g_state.first_here = true; // coop-houseenter (both: the host's, as before)
    g_state.first_here_zone = here;
    g_state.split_tick = g_state.tick;
    g_state.leaving = false;
    g_state.left_from = here;
    g_state.leaving_walk = 0;
    g_state.peer = here;
    g_state.peer_heard = false;
    g_state.peer_busy = false; // coop-mayorhouse
    g_state.peer_talk = false;
    g_state.peer_builds = false; // coop-buildarrive
    g_state.peer_sees_set = false;
    g_state.peer_ready = false;
    g_state.presence_due = std::chrono::steady_clock::now();
    // coop-fx51b (net-drop-kill-host on a fixture: "identity: player 2's copy draws its player as Toan" once the host died; Jep's
    // rule, she is never Toan unless she picks him): together, her copy's game player was his Toan and she the partner; on a
    // floor her copy plays on as her own member, as on any floor she comes onto apart (g_become_due: the floor's next tick of
    // play). DC_WORLDS_MUTATE=no_become (twin): Toan, as before.
    if (mode == GAME_MODE_DUNGEON && Player() == 2) {
        g_become_due = true;
    }
    std::snprintf(line, sizeof line, "the link dropped at tick %lld %s: apart in place (this copy, player %d, plays on; each copy's play goes to the other)",
                  (long long) g_state.tick, where, Player());
    Log(line);
    UpdateStatus();

    if (CUserStatus *floors = UserStatus != nullptr ? UserStatus : SaveData != nullptr ? (CUserStatus *) SaveData->GetDngStatus() : nullptr) {
        std::memcpy(g_state.floors_told, floors->floor_reached, sizeof g_state.floors_told);
        g_state.floors_set = true;
        g_state.party_told = floors->party_size;
    }

    slotsync::Begin();
    slotsync::BagDump("drop");
}

// What the game does with an atla it collects (getAtraToSaveData's body): the town's part or chip, and the
// floor's slot marked collected.
void AtlaToSave(int atra, int atra_no, CSaveData *save, int dungeon, int floor) {
    if (atra < 0x28) {
        save->AtraPartsGet(dungeon, atra);
    } else {
        save->AtraChipGet(dungeon, atra - 0x28);
    }

    // On a floor the party's status is UserStatus; in a town (the other copy's atla reaching it) the save's own.
    CDngStatusData *status = UserStatus != nullptr ? (CDngStatusData *) UserStatus : save->GetDngStatus();

    if (status != nullptr) {
        status->GetAtraData(dungeon, floor, atra_no);
    }
}

// coop-worlds: live sync while apart (Jep's 2026-10-21 rule: what both players share stays the same on both copies while
// they are apart, both ways, first to act wins). The shared slots below are compared each tick against what
// both copies had at the split; a changed slot goes to the other copy as a record (resent until acknowledged, applied
// once), with the version it was changed from and the apart clock's tick (both copies count it from the split's tick).
// A record from the version this copy also holds is applied; both copies changing a slot from the same version is a
// clash: the earlier tick wins (a tie: the host's), both copies keep the winner's, the loser is told.
// DC_WORLDS_MUTATE=no_slot_sync (twin): nothing is sent.
namespace slotsync {

constexpr std::uint32_t kSlotMagic = kMagic; // the worlds' own messages (netplay routes these); a slot record by its size
constexpr int           kMaxBytes = 0xF8;       // a WEAPON_HAVE, the largest slot
constexpr std::int64_t  kTieTicks = 6;          // changes this near on the apart clock (100 ms) are a tie: the host's

enum SlotKind : std::uint32_t {
    kSlot = 1,
    kSlotAck = 2,
};

enum Region : int {
    kStorageItem = 0, // the house's storage: item and its uses
    kStorageWeapon,
    kStorageAttach,
    kBagItem, // player 1's bag (the party's pack): item and its uses
    kBagHeader,
    kWeapon,   // each character's weapons, 6 x 11
    kEquipped, // each character's equipped weapon slot
    kCarriedAttach,
    kGilda,
    kGeoPlot,    // each town's Georama plots (6 x 24): obtained, placed, stock, elements
    kGeoPart,    // each town's placed parts (6 x 130)
    kGeoRequest, // each town's requests (6)
    kGeoNpc,     // each town's NPC slots (6 x 20)
    kGeoChip,    // each town's chips (6 x 128)
    kP2BagItem,  // coop-dungeonp2: player 2's own bag (separate bags): item and its uses
    kP2BagHeader,
    kP2Gilda,
    kRing,       // coop-worldkeys: the party's key ring (coop::g_ring, in no bag): item and its uses
    kQuest,      // coop-pt1111e: each quest's progress (quest/framework.cpp), its name and JSON (QuestSlotRead)
    kRegions
};

const char *const kRegionNames[kRegions] = {"storage item", "storage weapon", "storage attachment", "bag item", "bag header",
                                            "weapon",       "equipped weapon", "carried attachment", "gilda", "Georama plot",
                                            "Georama part", "Georama request", "Georama NPC",  "Georama chip",
                                            "player 2 bag item", "player 2 bag header", "player 2 gilda", "key ring", "quest"};
const int kRegionCount[kRegions] = {60, 30, 30, 103, 1, 66, 6, 43, 1, 6 * 24, 6 * 130, 6, 6 * 20, 6 * 128, 103, 1, 1, 103, 16};

#pragma pack(push, 1)
struct SlotMsg {
    std::uint32_t magic;
    std::uint32_t player;
    std::uint32_t kind;
    std::uint32_t id;      // the sender's own count, from 1
    std::int32_t  region;
    std::int32_t  index;
    std::uint32_t base;    // the slot's version it was changed from
    std::int64_t  tick;    // the apart clock's tick of the change
    std::int32_t  len;
    std::uint8_t  bytes[kMaxBytes];
};
#pragma pack(pop)

struct Pending {
    std::uint32_t base = 0;
    std::int64_t  tick = 0;
    bool          live = false; // this copy's own change, not yet settled with the other's
    bool          from_empty = false; // coop-apartstats: and it filled an empty slot of a list (SlotEmpty)
};

struct Sync {
    bool                                              on = false;
    std::vector<std::vector<std::uint8_t>>            baseline[kRegions];
    std::vector<std::uint32_t>                        version[kRegions];
    std::vector<Pending>                              mine[kRegions];
    std::vector<SlotMsg>                              out;
    std::unordered_set<std::uint32_t>                 applied;
    std::uint32_t                                     next_id = 0;
    std::int64_t                                      clock = 0; // the apart clock (time since the split, 60 a second)
    std::chrono::steady_clock::time_point             since{};  // the split
    std::chrono::steady_clock::time_point             due{};
    std::unordered_map<std::uint32_t, std::chrono::steady_clock::time_point> made; // when each record was made
    std::unordered_map<std::uint32_t, SlotMsg>                               sent; // every record this copy made, by id
    std::unordered_map<std::uint32_t, SlotMsg>                               late; // coop-catchuprace: hers, till its lockstep command
    int                                               scan = 0;
    int                                               parts_town = -1; // a town whose placed parts the other copy changed here
    int                                               parts_taken = 0; // coop-fx51c: the other copy's Georama part records applied here
};

Sync g_sync;

CUserStatus *Party() {
    return UserStatus != nullptr ? UserStatus : SaveData != nullptr ? (CUserStatus *) SaveData->GetDngStatus() : nullptr;
}

// coop-dungeonp2: player 2's copy apart with separate bags plays player 2's member as the game's own player, so
// the game's pack and money hold player 2's bag and purse there and g_bag holds player 1's (Jep: "Toan leaving
// swaps P2/P1 inventories"; her pickups and the menus used Toan's bag). Each bag's slots are read and written
// where that copy keeps them, so the live sync still moves player 1's bag to player 1's and player 2's to hers.
bool g_bags_swapped = false;

ITEM_PACK *PackOf(CUserStatus *party, int player) {
    return (player == 1) != g_bags_swapped ? &party->item_pack : &coop::g_bag.pack;
}

u16 *PurseOf(CUserStatus *party, int player) {
    return (player == 1) != g_bags_swapped ? &party->money : &coop::g_bag.gilda;
}

// Puts player 2's bag into the game's pack (want) or back. The quick-use slots' uses left go with the bag.
// DC_WORLDS_MUTATE=no_bag_swap (twin): never swaps, so her copy keeps playing from Toan's bag, as before.
void SwapBags(bool want) {
    CUserStatus *party = Party();

    if (want == g_bags_swapped || party == nullptr || (want && Mutated("no_bag_swap"))) {
        return;
    }

    std::swap(party->item_pack, coop::g_bag.pack);
    std::swap(party->money, coop::g_bag.gilda);

    for (int i = 0; i < 3; i++) {
        std::swap(party->active_item_vol[i], coop::g_bag.active_item_vol[i]);
    }

    g_bags_swapped = want;
    // coop-reds2 (l-local-play-houseout-hertalk, red on every PC-02 run): in a catch-up of her event apart, her event's swap
    // (CoopTownSwapDeferTick) lays her bag in each replayed tick and FollowApart takes it back at the next, two lines a tick on
    // the game thread. stderr is unbuffered: each line is a file write, and those writes were most of a replayed tick (8 of 8
    // stack samples here under load, 16 ms a tick; ~25 ms on PC-02), so his replay ran at or under her live 60 ticks a second
    // and never reached her. A replay's ticks say no swap line (as they take no shot line and no flush); the count follows.
    // DC_WORLDS_MUTATE=swap_lines_in_catchup (twin): every swap says its line, as before.
    static long quiet = 0;

    if (NetplayCatchUnderWay() && !Mutated("swap_lines_in_catchup")) {
        quiet++;
        return;
    }
    if (quiet > 0) {
        std::fprintf(stderr, "worlds: bags: %ld swaps in a catch-up said no line\n", quiet);
        quiet = 0;
    }
    std::fprintf(stderr, "worlds: bags %s: the game's pack is player %d's (%d gilda), player %d's beside it (%d gilda)\n",
                 want ? "swapped" : "back", want ? 2 : 1, (int) party->money, want ? 1 : 2, (int) coop::g_bag.gilda);
}

// coop-keyring: keys and story items are the party's, in player 1's pack, whoever picks them up. On her copy apart her
// bag is the game's pack, so what the game gives her player there (a floor's gate key, a script's story item) lands in
// her bag; each tick it moves on into player 1's pack (g_bag there), where both pickers look and the live sync carries
// it to player 1's copy. DC_WORLDS_MUTATE=no_ring_sweep (twin): it stays in her bag.
void RingSweep() {
    CUserStatus *party = Party();

    // coop-worldkeys: with the world's ring, a key left in either bag (a path that gave it past GetItem) goes into it.
    if (coop::RingOn() && party != nullptr) {
        coop::RingAdopt(party->item_pack, "the game's pack");
        coop::RingAdopt(coop::g_bag.pack, "the bag beside it");
        return;
    }

    if (!g_bags_swapped || party == nullptr || Mutated("no_ring_sweep")) {
        return;
    }

    ITEM_PACK &hers = party->item_pack;
    ITEM_PACK &ring = coop::g_bag.pack;

    for (int i = 0; i < 103; i++) {
        const int item = hers.item[i];

        if (item < ITEM_DUNGEON_START || !coop::SharedItem(item)) {
            continue;
        }

        int free = -1;

        for (int k = 0; k < 103 && free < 0; k++) {
            free = ring.item[k] < ITEM_DUNGEON_START ? k : -1;
        }

        if (free < 0) {
            continue;
        }

        ring.item[free] = hers.item[i];
        ring.item_vol[free] = hers.item_vol[i];
        hers.item[i] = -1;
        hers.item_vol[i] = 0;
        std::fprintf(stderr, "worlds: keyring: item %d from player 2's bag into the party's ring (player 1's pack)\n", item);
    }
}

// coop-mergebags: test hook DC_WORLDS_TEST_BAGDUMP=1: each player's bag (its carried items, quick-use slots and their
// uses left, purse) wherever this copy keeps it, and each character's equipped weapon and a hash of its weapons, as one
// line under a label (the split, the merge, the host's state taken, and every 600 ticks), for w-town-apart -Case bags.
void BagDump(const char *label) {
    static const bool on = TestEnv("DC_WORLDS_TEST_BAGDUMP") != nullptr;
    CUserStatus      *party = Party();

    statsync::Dump(label); // coop-apartstats (DC_WORLDS_TEST_STATDUMP)

    // coop-apartstats: DC_WORLDS_TEST_ATLADUMP=T: town T's chips held (how many, and how many of chip 0) and its first plot's part.
    static const int atla_town = TestEnvInt("DC_WORLDS_TEST_ATLADUMP", -1);

    if (atla_town >= 0 && atla_town < 6 && SaveData != nullptr && SaveData->GetElemData(atla_town) != nullptr) {
        const s16 *chips = SaveData->GetElemData(atla_town);
        int        held = 0, zero = 0;

        for (int i = 0; i < 128; i++) {
            held += chips[i] >= 0 ? 1 : 0;
            zero += chips[i] == 0 ? 1 : 0;
        }

        const SV_EDIT_PARTS_INFO *plot = SaveData->GetEditPartsInfo(atla_town, 0);

        std::fprintf(stderr, "worlds: atladump %s (player %d, %s): town %d chips %d chip0 %d plot0 %d %d\n", label, Player(), g_state.apart ? "apart" : "together",
                     atla_town, held, zero, plot != nullptr ? (int) plot->obtained : -1, plot != nullptr ? (int) plot->stock : -1);
        std::fflush(stderr);
    }

    if (!on || party == nullptr) {
        return;
    }

    std::string line;
    char        part[64];

    for (int player = 1; player <= 2; player++) {
        const ITEM_PACK    *pack = PackOf(party, player);
        const std::int32_t *vols = (player == 1) != g_bags_swapped ? party->active_item_vol : coop::g_bag.active_item_vol;

        std::snprintf(part, sizeof part, "%sp%d g=%d q=", player == 1 ? "" : " | ", player, (int) *PurseOf(party, player));
        line += part;

        for (int k = 0; k < 3; k++) {
            std::snprintf(part, sizeof part, "%s%d/%d/%d", k > 0 ? "," : "", (int) pack->quick_item_slot[k], (int) pack->quick_item_qty[k],
                          (int) vols[k]);
            line += part;
        }

        line += " items=";

        for (int i = 0; i < 103; i++) {
            if (pack->item[i] >= ITEM_DUNGEON_START) {
                std::snprintf(part, sizeof part, "%d:%d;", (int) pack->item[i], (int) pack->item_vol[i]);
                line += part;
            }
        }
    }

    // coop-bags: the house's storage (the check-in counter's board, party-shared), item:uses by slot, for the storage rows to compare.
    line += " | storage=";
    if (const CStockItem *stock = SaveData != nullptr ? SaveData->GetStockItem() : nullptr; stock != nullptr) {
        for (int i = 0; i < 60; i++) {
            if (stock->dungeon_items[i] >= ITEM_DUNGEON_START) {
                std::snprintf(part, sizeof part, "%d@%d:%d;", i, (int) stock->dungeon_items[i], (int) stock->dungeon_item_vols[i]);
                line += part;
            }
        }
    }

    line += " | weapons";

    for (int c = 0; c < 6; c++) {
        const int     slot = party->equipped_weapon_slot[c];
        std::uint32_t hash = 2166136261u;
        const auto   *raw = reinterpret_cast<const unsigned char *>(party->chara_weapons[c]);

        for (std::size_t i = 0; i < sizeof party->chara_weapons[c]; i++) {
            hash = (hash ^ raw[i]) * 16777619u;
        }

        std::snprintf(part, sizeof part, " c%d=%d:%d:%08x", c, slot,
                      slot >= 0 && slot < 11 ? (int) party->chara_weapons[c][slot].item_no : -1, (unsigned) hash);
        line += part;
    }

    // (coop-pt1109e: the party's carried attachments, one list for both players: what a scene's _SITEM_GET of an attachment gives)
    line += " | carried=";
    for (const auto &carried : party->consumable_items) {
        if (carried.id >= ITEM_ATTACH_START) {
            std::snprintf(part, sizeof part, "%d;", (int) carried.id);
            line += part;
        }
    }

    std::fprintf(stderr, "worlds: bagdump %s (player %d, %s, swapped %d): %s\n", label, Player(), g_state.apart ? "apart" : "together",
                 (int) g_bags_swapped, line.c_str());

    // coop-mergesafe: DC_WORLDS_TEST_FLAGDUMP=F[,F...]: these game flags as this copy holds them, with each bag dump (the
    // mayor's scene sets flag 0; a merge that lost it plays the scene again).
    static const char *const flags = TestEnv("DC_WORLDS_TEST_FLAGDUMP");

    if (flags != nullptr && SaveData != nullptr) {
        std::string list;

        for (const char *p = flags; *p != '\0';) {
            char *end = nullptr;
            long  flag = std::strtol(p, &end, 10);

            if (end == p) {
                break;
            }
            std::snprintf(part, sizeof part, " game flag %ld = %d;", flag, SaveData->GetGameFlag((int) flag));
            list += part;
            p = *end == ',' ? end + 1 : end;
        }
        std::fprintf(stderr, "worlds: flagdump %s (player %d, %s):%s\n", label, Player(), g_state.apart ? "apart" : "together", list.c_str());
    }
    std::fflush(stderr);
}

// A slot's bytes now (len 0: not readable here).
// coop-pt1111e (coordinator, 11-11): the quests' progress apart. A quest that moves on one copy apart (its NPC's choice, its rule)
// was this copy's alone: the merge took the stayer's state set, so the other's move was lost, or (with the progress in the checksum)
// parted them. Each quest is a slot: its name, a NUL, its progress as JSON (stage and variables), a NUL, at most kMaxBytes; the
// slots in the quests' name order (SaveJson's object), alike on every copy. Read from one SaveJson per scan (slot 0 makes it).
// DC_WORLDS_MUTATE=no_quest_slot (twin): no quest slot read, so no record, as before.
std::vector<std::string> g_quest_slots;

void QuestSlotsRefresh() {
    g_quest_slots.clear();
    const nlohmann::json saved = nlohmann::json::parse((dc::quest::QuestHostSetup(), dc::quest::SaveJson()), nullptr, false);

    if (saved.is_discarded() || !saved.contains("quests") || !saved["quests"].is_object()) {
        return;
    }
    for (const auto &[name, progress] : saved["quests"].items()) {
        std::string slot = name;

        slot.push_back('\0');
        slot += progress.dump();
        slot.push_back('\0');
        g_quest_slots.push_back(slot.size() <= (std::size_t) kMaxBytes ? slot : std::string());
    }
}

int QuestSlotRead(int index, std::uint8_t *out) {
    if (Mutated("no_quest_slot")) {
        return 0;
    }
    if (index == 0 || g_quest_slots.empty()) {
        QuestSlotsRefresh();
    }
    if (index >= (int) g_quest_slots.size() || g_quest_slots[(std::size_t) index].empty()) {
        return 0;
    }
    const std::string &slot = g_quest_slots[(std::size_t) index];

    std::memcpy(out, slot.data(), slot.size());
    return (int) slot.size();
}

void QuestSlotWrite(const std::uint8_t *in) {
    const char *name = reinterpret_cast<const char *>(in);
    const std::size_t name_len = strnlen(name, kMaxBytes);

    if (name_len >= (std::size_t) kMaxBytes - 1) {
        return;
    }
    const char *text = name + name_len + 1;
    const std::string progress(text, strnlen(text, kMaxBytes - name_len - 1));
    nlohmann::json saved = nlohmann::json::parse((dc::quest::QuestHostSetup(), dc::quest::SaveJson()), nullptr, false);
    const nlohmann::json value = nlohmann::json::parse(progress, nullptr, false);

    if (saved.is_discarded() || value.is_discarded() || !saved.contains("quests")) {
        std::fprintf(stderr, "worlds: sync: a quest record that does not read (%s)\n", name);
        return;
    }
    saved["quests"][std::string(name, name_len)] = value;
    dc::quest::LoadJson(saved.dump());
    g_quest_slots.clear();
}

int Read(int region, int index, std::uint8_t *out) {
    CUserStatus *party = Party();
    CStockItem  *stock = SaveData != nullptr ? SaveData->GetStockItem() : nullptr;

    if (party == nullptr || stock == nullptr) {
        return 0;
    }

    switch (region) {
        case kStorageItem:
            std::memcpy(out, &stock->dungeon_items[index], 2);
            std::memcpy(out + 2, &stock->dungeon_item_vols[index], 2);
            return 4;
        case kStorageWeapon:
            std::memcpy(out, &stock->weapons[index], sizeof(WEAPON_HAVE));
            return sizeof(WEAPON_HAVE);
        case kStorageAttach:
            std::memcpy(out, &stock->attachments[index], sizeof(ATTACH_LIST));
            return sizeof(ATTACH_LIST);
        case kBagItem:
        case kP2BagItem: {
            ITEM_PACK *pack = PackOf(party, region == kBagItem ? 1 : 2);

            if (region == kP2BagItem && !coop::OwnBags()) {
                return 0;
            }

            std::memcpy(out, &pack->item[index], 2);
            std::memcpy(out + 2, &pack->item_vol[index], 2);
            return 4;
        }
        case kRing:
            if (!coop::RingOn()) {
                return 0;
            }

            std::memcpy(out, &coop::SharedRing().item[index], 2);
            std::memcpy(out + 2, &coop::SharedRing().item_vol[index], 2);
            return 4;
        case kBagHeader:
        case kP2BagHeader:
            if (region == kP2BagHeader && !coop::OwnBags()) {
                return 0;
            }

            std::memcpy(out, PackOf(party, region == kBagHeader ? 1 : 2), 14); // num, item_count, the quick-use slots and their counts
            return 14;
        case kWeapon:
            std::memcpy(out, &party->chara_weapons[index / 11][index % 11], sizeof(WEAPON_HAVE));
            return sizeof(WEAPON_HAVE);
        case kEquipped:
            out[0] = (std::uint8_t) party->equipped_weapon_slot[index];
            return 1;
        case kCarriedAttach:
            std::memcpy(out, &party->consumable_items[index], sizeof(DNG_CONSUMABLE));
            return sizeof(DNG_CONSUMABLE);
        case kGilda:
        case kP2Gilda:
            if (region == kP2Gilda && !coop::OwnBags()) {
                return 0;
            }

            std::memcpy(out, PurseOf(party, region == kGilda ? 1 : 2), 2);
            return 2;
        case kGeoPlot:
            std::memcpy(out, &SaveData->GetGrdData(index / 24)->parts_info[index % 24], sizeof(SV_EDIT_PARTS_INFO));
            return sizeof(SV_EDIT_PARTS_INFO);
        case kGeoPart:
            std::memcpy(out, &SaveData->GetGrdData(index / 130)->placed_parts[index % 130], sizeof(SV_GRD_PART));
            return sizeof(SV_GRD_PART);
        case kGeoRequest: {
            SV_GEORAMA_DATA *town = SaveData->GetGrdData(index);

            std::memcpy(out, &town->request_count, 4);
            std::memcpy(out + 4, town->request_complete, sizeof town->request_complete);
            std::memcpy(out + 4 + sizeof town->request_complete, &town->request_event_flag, 4);
            return 8 + (int) sizeof town->request_complete;
        }
        case kGeoNpc:
            std::memcpy(out, &SaveData->GetGrdData(index / 20)->npc[index % 20], sizeof(SV_GRD_NPC));
            return sizeof(SV_GRD_NPC);
        case kGeoChip:
            std::memcpy(out, &SaveData->GetElemData(index / 128)[index % 128], 2);
            return 2;
        case kQuest:
            return QuestSlotRead(index, out);
        default:
            return 0;
    }
}

void Write(int region, int index, const std::uint8_t *in) {
    CUserStatus *party = Party();
    CStockItem  *stock = SaveData != nullptr ? SaveData->GetStockItem() : nullptr;

    if (party == nullptr || stock == nullptr) {
        return;
    }

    switch (region) {
        case kStorageItem:
            std::memcpy(&stock->dungeon_items[index], in, 2);
            std::memcpy(&stock->dungeon_item_vols[index], in + 2, 2);
            break;
        case kStorageWeapon:
            std::memcpy(&stock->weapons[index], in, sizeof(WEAPON_HAVE));
            break;
        case kStorageAttach:
            std::memcpy(&stock->attachments[index], in, sizeof(ATTACH_LIST));
            break;
        case kBagItem:
        case kP2BagItem: {
            ITEM_PACK *pack = PackOf(party, region == kBagItem ? 1 : 2);

            std::memcpy(&pack->item[index], in, 2);
            std::memcpy(&pack->item_vol[index], in + 2, 2);
            break;
        }
        case kRing:
            std::memcpy(&coop::SharedRing().item[index], in, 2);
            std::memcpy(&coop::SharedRing().item_vol[index], in + 2, 2);
            break;
        case kBagHeader:
        case kP2BagHeader:
            std::memcpy(PackOf(party, region == kBagHeader ? 1 : 2), in, 14);
            coop::BagSizeNow(); // coop-bags: the other copy's upgrade arrived in a header (its num): every bag takes the party's size now, not at a later tick
            break;
        case kWeapon:
            std::memcpy(&party->chara_weapons[index / 11][index % 11], in, sizeof(WEAPON_HAVE));
            break;
        case kEquipped:
            party->equipped_weapon_slot[index] = (s8) in[0];
            break;
        case kCarriedAttach:
            std::memcpy(&party->consumable_items[index], in, sizeof(DNG_CONSUMABLE));
            break;
        case kGilda:
        case kP2Gilda:
            std::memcpy(PurseOf(party, region == kGilda ? 1 : 2), in, 2);
            break;
        case kGeoPlot:
            std::memcpy(&SaveData->GetGrdData(index / 24)->parts_info[index % 24], in, sizeof(SV_EDIT_PARTS_INFO));
            // DC_WORLDS_MUTATE=no_georama_refresh (twin): the town's working plot is left as it was, so its next save puts it back.
            if (mode == GAME_MODE_EDIT && !Mutated("no_georama_refresh")) {
                CoopTownGeoramaPlotFromSave(index / 24, index % 24); // the town this copy is in: its working plot too
            }
            break;
        case kGeoPart:
            std::memcpy(&SaveData->GetGrdData(index / 130)->placed_parts[index % 130], in, sizeof(SV_GRD_PART));
            break;
        case kGeoRequest: {
            SV_GEORAMA_DATA *town = SaveData->GetGrdData(index);

            std::memcpy(&town->request_count, in, 4);
            std::memcpy(town->request_complete, in + 4, sizeof town->request_complete);
            std::memcpy(&town->request_event_flag, in + 4 + sizeof town->request_complete, 4);
            break;
        }
        case kGeoNpc:
            std::memcpy(&SaveData->GetGrdData(index / 20)->npc[index % 20], in, sizeof(SV_GRD_NPC));
            break;
        case kGeoChip:
            std::memcpy(&SaveData->GetElemData(index / 128)[index % 128], in, 2);
            break;
        case kQuest:
            QuestSlotWrite(in);
            break;
        default:
            break;
    }
}

// coop-geopart (Jep's 2026-11-04 pack 8 logs: both apart in one town, Georama part 19 sent back and forth every 500 ms):
// a placed part the other copy changed is in this copy's save alone; the town's live parts are the ones the game draws and
// saves, so the next CoopTownGeoramaToSave put this copy's old part back over it and sent that as a change, and so on, the
// last to save winning (her part move lost). The save's part list is packed (a part taken out moves the rest down), so no
// one live part stands for a slot: the town's live parts are rebuilt from the save, as the game's town load does (EditLoad),
// once for all the records taken since (a part taken out arrives as several), before this copy's next look at its town.
// DC_WORLDS_MUTATE=no_part_refresh (twin): the live parts left as they were, so the next save puts them back.
void PartsFromSave() {
    const int town = g_sync.parts_town;

    if (town < 0) {
        return;
    }
    if (mode != GAME_MODE_EDIT || MapNo < 0 || MapNo >= TOWN_COUNT || NowEditMap != town || pEditGround == nullptr || SaveData == nullptr) {
        g_sync.parts_town = -1; // not in that town: it loads its parts from the save as this copy goes there
        return;
    }

    g_sync.parts_town = -1;

    if (Mutated("no_part_refresh")) {
        return;
    }

    pEditGround->Load(town, SaveData);
    EditPartsObjectOnOff();
    pEditGround->MakePartsBox();
    std::fprintf(stderr, "worlds: sync: town %d's live Georama parts rebuilt from the save (the other copy's part change)\n", town);
}

// The town's working Georama into the save, the other copy's part changes into it first (PartsFromSave).
void TownToSave() {
    PartsFromSave();
    CoopTownGeoramaToSave();
}

void ApplyLate(int player, const NetCommand &command, std::int64_t tick);

// At the split (both copies alike): every slot as both hold it, version 0.
void Begin() {
    std::uint8_t bytes[kMaxBytes];

    NetplaySetCommandHandler(kCmdSlot, ApplyLate); // coop-catchuprace
    NetplaySetCommandHandler(kCmdProgress, ApplyLateProgress);
    g_sync = Sync{};
    g_sync.on = !Mutated("no_slot_sync");
    g_sync.since = std::chrono::steady_clock::now();

    // In a town the working Georama can be newer than the save's: the baseline is what both copies hold now.
    if (mode == GAME_MODE_EDIT) {
        TownToSave();
    }

    for (int r = 0; r < kRegions; r++) {
        g_sync.baseline[r].resize(kRegionCount[r]);
        g_sync.version[r].assign(kRegionCount[r], 0);
        g_sync.mine[r].assign(kRegionCount[r], Pending{});

        for (int i = 0; i < kRegionCount[r]; i++) {
            const int len = Read(r, i, bytes);

            g_sync.baseline[r][i].assign(bytes, bytes + len);
        }
    }

    statsync::Begin(); // coop-apartstats: every split goes through here
}

// coop-apartstats: whether a slot of a list region holds nothing (its first halfword: the item, weapon, attachment or chip
// there). Not a list (a header, a purse, Georama plots, parts, requests, NPCs): never empty.
bool SlotEmpty(int region, const std::uint8_t *bytes, int len) {
    std::int16_t id = 0;

    if (len < 2) {
        return false;
    }
    std::memcpy(&id, bytes, 2);

    switch (region) {
        case kStorageItem:
        case kBagItem:
        case kP2BagItem:
            return id < ITEM_DUNGEON_START;
        case kStorageWeapon:
        case kWeapon:
            return id < ITEM_WEAPON_START;
        case kStorageAttach:
        case kCarriedAttach:
            return id < 81; // DNG_CONSUMABLE: below 81 an empty slot
        case kGeoChip:
            return id < 0;
        default:
            return false;
    }
}

// The run of slots one list is (a character's 11 weapons, a town's 128 chips; else the whole region).
int ListSize(int region) {
    return region == kWeapon ? 11 : region == kGeoChip ? 128 : kRegionCount[region];
}

void Send(const SlotMsg &msg) {
    NetSocketSend(&msg, sizeof msg);
}

void Scan(std::chrono::steady_clock::time_point now);

// coop-fx51c test hook (DC_WORLDS_TEST_SLOT_AT_MERGE_WHEN=catchup, with DC_WORLDS_TEST_SLOT_AT_MERGE): the test's record, made as the
// merge's link starts, is held from the merge's start till this copy's catch-up begins (the host's checkpoint heard,
// CoopWorldsTestSlotOnCatch) and goes then, so it reaches the host after its checkpoint and its join every run, and a later
// blob carries it back (w-world-split -LateChange on a fixture: sent at once, it reached the host before its join).
bool g_test_hold_to_catch = false;
// coop-catchuprace: =lastblob holds it on till the host's last blob (it names J) is here, so it reaches the host after that
// blob every run (train 54's race, which a quick catch-up hit now and then): no blob carries it, the lockstep does.
bool TestHoldsToCatch() {
    static const bool on = TestEnvIs("DC_WORLDS_TEST_SLOT_AT_MERGE_WHEN", "catchup") || TestEnvIs("DC_WORLDS_TEST_SLOT_AT_MERGE_WHEN", "lastblob");

    return on && g_test_merge_started && !g_test_hold_to_catch;
}
void SendHeldToCatch() {
    g_test_hold_to_catch = true;
    for (const SlotMsg &msg : g_sync.out) {
        Send(msg);
    }
    std::fflush(stderr);
}

// Each tick apart: the apart clock; twice a second, this copy's changed slots out and what is unacknowledged again.
void Tick() {
    if (!g_state.apart || Player() == 0) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();

    // The apart clock: the split's tick plus the time since it, at 60 a second. Both copies split on the same tick,
    // so it reads alike on both, however each copy's own ticks stall (a load, a slow frame).
    g_sync.clock = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_sync.since).count() * 60 / 1000;
    const bool send = now >= g_sync.due;

    if (send) {
        g_sync.due = now + std::chrono::milliseconds(500);
    }

    // The slots each tick, so a record's tick is when the change happened (first to act wins by it); the town's
    // working Georama (saved only as the town is left) twice a second, with the sends.
    PartsFromSave(); // each tick, so this copy's town shows it at once
    if (send && mode == GAME_MODE_EDIT) {
        TownToSave();
    }

    Scan(now);

    // DC_WORLDS_TEST_SYNC_DELAY=MS (a test's): each record held that long before its first send, so both copies can
    // change a slot before either hears of the other's (a clash on purpose).
    static const int hold_ms = TestEnvInt("DC_WORLDS_TEST_SYNC_DELAY", 0);

    for (const SlotMsg &msg : g_sync.out) {
        if (!send || TestHoldsToCatch()) {
            break;
        }

        if (hold_ms > 0 && now < g_sync.made[msg.id] + std::chrono::milliseconds(hold_ms)) {
            continue;
        }

        Send(msg);
    }

    std::fflush(stderr);
}

// coop-apartstats: this copy's changes up to now made records (the town's working Georama first), before something from the
// other copy changes the same slots on purpose (an Atla's, Settle).
void Flush() {
    if (!g_state.apart || Player() == 0 || (NetplayCatchUnderWay() && !Mutated("scan_in_catchup"))) {
        return;
    }
    if (mode == GAME_MODE_EDIT) {
        TownToSave();
    }
    Scan(std::chrono::steady_clock::now());
}

// coop-apartstats: an Atla changes its town's plot or chips on both copies by the progress channel (each copy calls the game's
// own getAtraToSaveData body once), so those slots are what both copies hold now: settled, not a record. A record of it as
// well put it on the other copy twice whenever it landed before the progress (a lost progress send, resent 500 ms later):
// a stackable part's stock +10, a chip in two slots. DC_WORLDS_MUTATE=atla_slot_echo (twin): a record too, as before.
void Settle(int town) {
    if (!g_state.apart || town < 0 || town >= 6 || Mutated("atla_slot_echo")) {
        return;
    }

    std::uint8_t bytes[kMaxBytes];
    const struct {
        int region, first, count;
    } runs[2] = {{kGeoPlot, town * 24, 24}, {kGeoChip, town * 128, 128}};

    for (const auto &run : runs) {
        for (int i = run.first; i < run.first + run.count && i < (int) g_sync.baseline[run.region].size(); i++) {
            const int len = Read(run.region, i, bytes);

            g_sync.baseline[run.region][i].assign(bytes, bytes + len);
        }
    }
}

// The slots against what this copy last held: each changed one a record for the other copy.
void Scan(std::chrono::steady_clock::time_point now) {
    std::uint8_t bytes[kMaxBytes];

    // Player 2's copy catching up to the host makes no more records: the host's state has replaced (or is about to replace)
    // its own, so what it would see as "changed here" is the host's, sent back late and applied over the host's own play
    // after its checkpoint, the host's live plots overwritten with them (T24, desync B). What it changed before the catch-up
    // began still goes out.
    // coop-catchuprace: nor once the catch-up's lockstep has begun (either copy, up to its merge tick): both copies play the
    // same ticks now, and her slots, the host's save, would go back as stale "changes" over a late record the lockstep applies.
    const bool catching_up = (Player() == 2 && NetplayCatchUnderWay()) ||
                             (NetplayLinkedByCatchup() && !Mutated("late_slot_direct"));

    for (int r = 0; g_sync.on && !catching_up && r < kRegions; r++) {
        for (int i = 0; i < kRegionCount[r] && i < (int) g_sync.baseline[r].size(); i++) {
            // coop-small2 (storage C): apart the host owns the storage; another player's copy changes it only by the host's answers and
            // records (CoopWorldsSlotRebase), so it sends none of its own. DC_COOP_MUTATE=storage_retail: as before.
            if (r <= kStorageAttach && Player() != 1 && CoopStorageOn()) {
                continue;
            }
            const int len = Read(r, i, bytes);
            auto     &base = g_sync.baseline[r][i];

            if (len == 0 || (len == (int) base.size() && std::memcmp(bytes, base.data(), len) == 0)) {
                continue;
            }

            SlotMsg msg{};

            msg.magic = kSlotMagic;
            msg.player = (std::uint32_t) Player();
            msg.kind = kSlot;
            msg.id = ++g_sync.next_id;
            msg.region = r;
            msg.index = i;
            msg.base = g_sync.version[r][i];
            msg.tick = g_state.split_tick + g_sync.clock;
            msg.len = len;
            std::memcpy(msg.bytes, bytes, len);

            g_sync.mine[r][i] = Pending{msg.base, msg.tick, true, SlotEmpty(r, base.data(), (int) base.size()) && !SlotEmpty(r, bytes, len)};
            g_sync.version[r][i]++;
            base.assign(bytes, bytes + len);
            g_sync.out.push_back(msg);
            g_sync.made[msg.id] = now;
            g_sync.sent[msg.id] = msg;
            std::fprintf(stderr, "worlds: sync: %s %d changed here (from version %u at tick %lld), record %u for player %d\n", kRegionNames[r], i,
                         msg.base, (long long) msg.tick, msg.id, 3 - Player());
        }
    }
}

// coop-apartstats: both copies filled the same empty slot of a list apart (each its own pickup into the first free slot of
// player 1's pack, the carried attachments or the storage) and the other's won the clash: this copy's thing is another
// thing, not the same one taken twice, so it moves to the list's next free slot here (the next scan's record takes it to the
// other copy) instead of being written over. False when there was nothing to keep or no room.
// DC_WORLDS_MUTATE=slot_clash_drop (twin): written over, as before (her pickup lost).
bool KeepClashed(int r, int i) {
    std::uint8_t mine[kMaxBytes];
    std::uint8_t other[kMaxBytes];
    const int    len = Read(r, i, mine);
    const int    first = i - i % ListSize(r);

    if (!g_sync.mine[r][i].from_empty || Mutated("slot_clash_drop") || len == 0 || SlotEmpty(r, mine, len)) {
        return false;
    }

    for (int j = first; j < first + ListSize(r) && j < (int) g_sync.baseline[r].size(); j++) {
        if (j == i || g_sync.mine[r][j].live || Read(r, j, other) != len || !SlotEmpty(r, other, len)) {
            continue;
        }

        Write(r, j, mine);
        // A dungeon item more in that pack (its header counts them).
        if (CUserStatus *party = Party(); party != nullptr && (r == kBagItem || r == kP2BagItem)) {
            PackOf(party, r == kBagItem ? 1 : 2)->item_count++;
        }
        std::fprintf(stderr, "worlds: sync: kept this copy's %s %d: both filled that empty slot apart, this copy's moves to %d\n", kRegionNames[r], i, j);
        return true;
    }

    std::fprintf(stderr, "worlds: sync: this copy's %s %d lost: both filled that empty slot apart and the list has no free slot\n", kRegionNames[r], i);
    return false;
}

// Her record taken on this copy (the host's): written over this copy's slot, and (at once, not by the lockstep) recorded for a
// catch-up's replay.
void TakeTheirs(const SlotMsg &msg, bool record) {
    const int   r = msg.region;
    const int   i = msg.index;
    Pending    &mine = g_sync.mine[r][i];
    const char *name = kRegionNames[r];

    mine.live = false;
    Write(r, i, msg.bytes);
    if (r == kGeoPart) {
        g_sync.parts_town = i / 130; // coop-geopart: the town's live parts from the save before its next save (PartsFromSave)
        g_sync.parts_taken++;        // coop-fx51c: the drop hook's event (DC_WORLDS_TEST_PART_DROP=taken:TOWN)
    }
    g_sync.baseline[r][i].assign(msg.bytes, msg.bytes + msg.len);
    std::fprintf(stderr, "worlds: sync: %s %d from player %u applied (record %u)\n", name, i, msg.player, msg.id);
    BagDump("sync applied"); // coop-small2: what this copy holds after it (DC_WORLDS_TEST_BAGDUMP; x-town-storage -Apart)

    // The host recording for a catch-up join: player 2's replay applies her own record again on this tick, as
    // the host's play does not make it. DC_WORLDS_MUTATE=no_slot_catchup (twin): not recorded.
    if (record && Player() == 1 && !Mutated("no_slot_catchup")) {
        const std::int32_t delta[5] = {kWhatSlot, (std::int32_t) msg.id, 0, 0, 0};

        NetplayCatchupDelta(delta);
    }
}

void Take(const SlotMsg &msg) {
    SlotMsg ack{};

    ack.magic = kSlotMagic;
    ack.player = (std::uint32_t) Player();
    ack.kind = kSlotAck;
    ack.id = msg.id;
    Send(ack);

    // coop-mergebags: player 2's copy catching up takes the host's slots from its replay, which ends where the host is;
    // a record written into the replay would land in the checkpoint's older state.
    if (Player() == 2 && CatchupUnderWay() && !Mutated("scan_in_catchup")) {
        return;
    }
    // coop-loadpull: her slot changes after his load are not put into his loaded save (TakeProgress, likewise).
    if (Player() == 1 && g_state.load_pull && !Mutated("load_no_pull")) {
        std::fprintf(stderr, "worlds: sync: player 2's %s %d dropped: player 1 loaded a save\n", msg.region >= 0 && msg.region < kRegions ? kRegionNames[msg.region] : "?",
                     msg.index);
        return;
    }

    // coop-catchuprace: her record can reach the host after its merge tick too (it left apart then): taken by the lockstep,
    // not dropped (she took his save at the lockstep, so dropped it was lost on both).
    const bool late = Player() == 1 && msg.player == 2 && NetplayCatchLastBlobGone() && !Mutated("late_slot_direct");

    if ((!g_state.apart && !late) || msg.region < 0 || msg.region >= kRegions || msg.index < 0 || msg.index >= kRegionCount[msg.region] ||
        msg.index >= (int) g_sync.baseline[msg.region].size() || !g_sync.applied.insert(msg.id).second) {
        return;
    }

    const int      r = msg.region;
    const int      i = msg.index;
    Pending       &mine = g_sync.mine[r][i];
    const char    *name = kRegionNames[r];
    bool           theirs = false;

    if (msg.base == g_sync.version[r][i]) {
        theirs = true; // this copy has not changed it since: theirs follows on from what both had
        g_sync.version[r][i]++;
    } else if (mine.live && msg.base == mine.base) {
        // Both changed it from the same version: the earlier wins; within kTieTicks of each other (the copies' apart
        // clocks start on their own runs of the split's tick, a lockstep's skew apart) a tie, the host's. Both decide alike.
        const bool mine_first = mine.tick + kTieTicks < msg.tick || (msg.tick + kTieTicks >= mine.tick && Player() == 1);

        theirs = !mine_first;
        std::fprintf(stderr, "worlds: sync: clash on %s %d (this copy's at tick %lld, player %u's at %lld): %s wins\n", name, i,
                     (long long) mine.tick, msg.player, (long long) msg.tick, mine_first ? "this copy's" : "theirs");

        if (theirs && KeepClashed(r, i)) {
            // coop-apartstats: nothing lost, nobody to tell
        } else if (theirs) {
            char notice[96];

            std::snprintf(notice, sizeof notice, "Player %u got there first (%s)", msg.player, name);
            NetplayShowNotice(notice);
        }
    }

    // coop-catchuprace (train 54's w-world-merge-catchup-late red): her record reached the host after its catch-up's last blob
    // (the host's catch-up was quick: its last blob went ~58 ms before her record came). Applied here at once, it was in his
    // play from that moment and in no blob, so her replay never had it: OUT OF SYNC at the next checksum, a resync to mend
    // it. Now it goes into the lockstep as his command and both copies apply it on that command's tick (hers from her own
    // record, kept in g_sync.sent). Nobody waits: the command rides his next input frame. DC_WORLDS_MUTATE=late_slot_direct
    // (twin): applied here at once, as before.
    if (theirs && late) {
        NetCommand command;

        command.kind = kCmdSlot;
        command.c = msg.id;
        g_sync.late[msg.id] = msg;
        std::fprintf(stderr, "worlds: sync: %s %d from player %u (record %u) came after the catch-up's last blob: it goes by the lockstep\n", name, i,
                     msg.player, msg.id);
        NetplayQueueCommand(command);
    } else if (theirs) {
        TakeTheirs(msg, true);
    }

    std::fflush(stderr);
}

const char *Replay(std::uint32_t id);

// coop-catchuprace: kCmdSlot applied, on both copies on one lockstep tick: the host takes her record as Take would have,
// her copy writes its own record again (it was made before the checkpoint replaced her state).
void ApplyLate(int player, const NetCommand &command, std::int64_t tick) {
    const std::uint32_t id = command.c;

    if (Player() == 1) {
        auto it = g_sync.late.find(id);

        if (it == g_sync.late.end()) {
            std::fprintf(stderr, "worlds: sync: late record %u not held at tick %lld\n", id, (long long) tick);
            return;
        }
        TakeTheirs(it->second, false);
        g_sync.late.erase(it);
    } else {
        const char *line = Replay(id);
        auto        it = g_sync.sent.find(id);

        if (it != g_sync.sent.end() && it->second.region >= 0 && it->second.region < kRegions &&
            it->second.index < (int) g_sync.baseline[it->second.region].size()) {
            g_sync.baseline[it->second.region][it->second.index].assign(it->second.bytes, it->second.bytes + it->second.len);
            g_sync.mine[it->second.region][it->second.index].live = false;
        }
        std::fprintf(stderr, "worlds: sync: this copy's record %u %s\n", id, line);
    }
    std::fprintf(stderr, "worlds: sync: late record %u applied by the lockstep at tick %lld (player %d's command)\n", id, (long long) tick, player);
    std::fflush(stderr);
}

bool Receive(const void *data, int size) {
    if (size != (int) sizeof(SlotMsg)) {
        return false;
    }

    SlotMsg msg;

    std::memcpy(&msg, data, sizeof msg);

    if (msg.magic != kSlotMagic || msg.player != (std::uint32_t) (3 - Player())) {
        return false;
    }

    // coop-floorsnap: a joiner that has the host's snapshot in holds the host's save as of S, and its replay makes every
    // change of the host's since; a record of one of them would land twice (w-floor-pick: the weapon's durability). The
    // joiner's own records keep going the other way: the host takes them and records their tick, as before.
    if (msg.kind == kSlot && NetplaySnapshotApplied()) {
        return true;
    }
    if (msg.kind == kSlot) {
        Take(msg);
    } else if (msg.kind == kSlotAck) {
        for (auto it = g_sync.out.begin(); it != g_sync.out.end(); ++it) {
            if (it->id == msg.id) {
                g_sync.out.erase(it);
                if (g_sync.out.empty()) {
                    BagDump("sync acked"); // coop-small2: every record of this copy's taken (DC_WORLDS_TEST_BAGDUMP; x-town-storage -Apart)
                }
                break;
            }
        }
    }

    return true;
}

bool Idle() {
    return g_sync.out.empty();
}

// A catch-up replay's kWhatSlot: this copy's own record id, applied again (player 2's, as the host took it).
const char *Replay(std::uint32_t id) {
    static char line[160];
    auto        it = g_sync.sent.find(id);

    if (it == g_sync.sent.end()) {
        std::snprintf(line, sizeof line, "slot record %u not found", id);
        return line;
    }

    const SlotMsg &msg = it->second;

    Write(msg.region, msg.index, msg.bytes);
    std::snprintf(line, sizeof line, "applied: %s %d again (this copy's record %u)", kRegionNames[msg.region], msg.index, id);
    return line;
}

// Test hook (DC_WORLDS_TEST_SLOT=K:REGION:INDEX:VALUE): K ticks into being apart, this copy's slot's first two bytes set
// to VALUE, as taking or putting an item would (REGION by number, as kRegionNames lists them).
void TestTick() {
    // coop-dungeonp2: test hook DC_WORLDS_TEST_GOLD=K:N: K ticks into being apart, this copy's player picks up N gold
    // (the game's own pickup adds it to the party's money); and every 300 ticks apart, each bag's purse as the bag
    // test reads it (player 1's and player 2's, wherever this copy keeps them).
    if (g_state.apart && coop::OwnBags() && Party() != nullptr) {
        static const char *gold = TestEnv("DC_WORLDS_TEST_GOLD");
        static bool        gold_done = false;
        long long          k = -1;
        int                n = 0;

        static long long frames = -1; // ticks of play apart (the apart clock is wall time; headless copies run faster)

        frames++;
        if (!gold_done && gold != nullptr && std::sscanf(gold, "%lld:%d", &k, &n) == 2 && frames >= k) {
            gold_done = true;
            Party()->money = (u16) std::min(0xFFFF, Party()->money + n);
            std::fprintf(stderr, "worlds: test: this copy's player picks up %d gold\n", n);

            // DC_WORLDS_TEST_SAVE=N: then a numbered save N (as the save screen writes one).
            static const int save_no = TestEnvInt("DC_WORLDS_TEST_SAVE", -1);

            if (save_no >= 0) {
                const bool saved = SaveSlotWriteNumbered(save_no, nullptr);
                std::fprintf(stderr, "worlds: test: this copy saved slot %d (%d)\n", save_no, (int) saved);
            }
        }

        // coop-itemsend: test hook DC_WORLDS_TEST_BAGSEND=K:ITEM: K ticks into being apart, the bag screen's send on this
        // copy (its player's bag is the game's pack, as in their own menu): ITEM into it, then Select on it.
        static const char *bagsend = TestEnv("DC_WORLDS_TEST_BAGSEND");
        static bool        bagsend_done = false;
        int                item = 0;

        if (!bagsend_done && bagsend != nullptr && std::sscanf(bagsend, "%lld:%d", &k, &item) == 2 && frames >= k) {
            bagsend_done = true;
            std::fprintf(stderr, "worlds: test: this copy (apart, player %d) sends item %d from the bag screen\n", Player(), item);
            coop::InventoryTestSend(Party()->item_pack, item);
        }

        // coop-bags: test hook DC_WORLDS_TEST_BAGUP=K:N:P (w-town-apart -Case bagup): K ticks into being apart the copy of player P
        // runs retail's own EdAddMaxItem(N) (a bag upgrade earned there, as the event scripts' _SADD_MAXITEM does); every copy says its
        // bag sizes 5 ticks before and 900 after (the live sync has carried it by then).
        static const char *bagup = TestEnv("DC_WORLDS_TEST_BAGUP");
        static int         bagup_stage = 0;
        int                up_n = 0, up_by = 0;

        if (bagup != nullptr && bagup_stage < 3 && std::sscanf(bagup, "%lld:%d:%d", &k, &up_n, &up_by) == 3) {
            if (bagup_stage == 0 && frames >= k - 5) {
                bagup_stage = 1;
                coop::BagSizesSay("apart before the upgrade");
            }
            if (bagup_stage == 1 && frames >= k) {
                bagup_stage = 2;
                if (Player() == up_by) {
                    std::fprintf(stderr, "worlds: test: this copy (apart, player %d) earns a bag upgrade of %d\n", Player(), up_n);
                    EdAddMaxItem(up_n);
                }
            }
            if (bagup_stage == 2 && frames >= k + 900) {
                bagup_stage = 3;
                coop::BagSizesSay("apart after the upgrade");
            }
        }

        // coop-mergebags: test hook DC_WORLDS_TEST_PICKUP=K:ITEM: K ticks into being apart, this copy's player picks up
        // ITEM (one use) into its own bag (the game's pack here, as the game's own pickup puts it).
        static const char *pickup = TestEnv("DC_WORLDS_TEST_PICKUP");
        static bool        pickup_done = false;

        if (!pickup_done && pickup != nullptr && std::sscanf(pickup, "%lld:%d", &k, &item) == 2 && frames >= k) {
            pickup_done = true;
            ITEM_PACK &pack = Party()->item_pack;

            for (int i = 0; i < pack.num && i < 103; i++) {
                if (pack.item[i] < ITEM_DUNGEON_START) {
                    pack.item[i] = (s16) item;
                    pack.item_vol[i] = 1;
                    break;
                }
            }

            std::fprintf(stderr, "worlds: test: this copy (apart, player %d) picks up item %d\n", Player(), item);
            BagDump("pickup");
        }

        static long long report_at = 0;

        if (frames >= report_at) {
            report_at = frames + 300;
            std::fprintf(stderr, "worlds: bags: player 1 gilda %d, player 2 gilda %d (swapped %d, apart tick %lld)\n",
                         (int) *PurseOf(Party(), 1), (int) *PurseOf(Party(), 2), (int) g_bags_swapped, (long long) frames);
        }
    }

    // coop-geopart: test hook DC_WORLDS_TEST_PART_PLACE=K: K ticks into being apart (the apart clock), this copy, in a town,
    // places the first building of the town's catalogue on the first free cell that takes it, as its Georama's Cross would
    // (in stock first). Its next save makes the records. coop-fx51c (Jep, 2026-11-04: no fixed tick waits): =heard, once this
    // copy apart has heard where the other is (its presence: the other copy is where it went, its records go to it).
    static const char *const place_spec = TestEnv("DC_WORLDS_TEST_PART_PLACE");
    static const bool        place_on_heard = place_spec != nullptr && std::strcmp(place_spec, "heard") == 0;
    if (static const int place_at = place_on_heard ? 0 : TestEnvInt("DC_WORLDS_TEST_PART_PLACE", -1);
        place_at >= 0 && g_state.apart && g_sync.clock >= place_at && (!place_on_heard || g_state.peer_heard)) {
        static bool done_place = false;

        if (!done_place && mode == GAME_MODE_EDIT && MapNo >= 0 && MapNo < TOWN_COUNT && pEditGround != nullptr && pEditGround->plot_parts != nullptr) {
            done_place = true;
            int placed = -1, plot = -1;
            CVector3_f_ at{};

            for (int i = 0; i < EditPartsInfo.parts_max && i < 24 && plot < 0; i++) {
                EDITPARTS_INFO *info = EditPartsInfo.GetPartsInfo(i);

                if (info != nullptr && info->header != nullptr && info->width > 0 && pEditGround->plot_parts[i].handle >= 0 &&
                    pEditGround->plot_parts[i].subtype == MAP_PARTS_SUBTYPE_NONE) {
                    plot = i;
                    info->obtained = 1;
                    info->stock = info->placed + 1;
                }
            }

            for (int a = 0; plot >= 0 && a < 4 && placed < 0; a++) {
                CEditArea *area = pEditGround->areas[a];

                for (int z = 0; area != nullptr && z < area->height && placed < 0; z++) {
                    for (int x = 0; x < area->width && placed < 0; x++) {
                        area->GetPos(&at, x, 0, z);
                        placed = pEditGround->SetMapParts(plot, at.x + 1.0f, at.y, at.z + 1.0f, 0); // as EditLoad's records go down
                    }
                }
            }

            EditPartsObjectOnOff();
            pEditGround->MakePartsBox();
            std::fprintf(stderr, "worlds: test: this copy (apart, player %d) places Georama plot %d in town %d: %s (%.0f %.0f) at apart tick %lld\n", Player(), plot,
                         NowEditMap, placed >= 0 ? "placed" : "no free cell", at.x, at.z, (long long) (g_state.split_tick + g_sync.clock));
            std::fflush(stderr);
        }
    }

    // coop-geopart: test hook DC_WORLDS_TEST_PART_DROP=K:TOWN: K ticks into being apart (the apart clock), this copy takes
    // TOWN's last placed part out of its save (its plot's placed count one less), as the Georama's own pick-up would. For a
    // copy that is not in that town (its live parts would save over it). coop-fx51c: =taken:TOWN, once this copy has applied
    // one of the other copy's part records (hers, placed in that town), not a tick count.
    if (static const char *drop = TestEnv("DC_WORLDS_TEST_PART_DROP"); drop != nullptr && g_state.apart && SaveData != nullptr) {
        static bool done_drop = false;
        long long   at = -1;
        int         town = -1, count = 0;
        const bool  on_taken = std::strncmp(drop, "taken:", 6) == 0;

        if (on_taken) {
            at = g_sync.parts_taken > 0 ? 0 : -1;
            town = std::atoi(drop + 6);
        }
        if (!done_drop && (on_taken || std::sscanf(drop, "%lld:%d", &at, &town) == 2) && at >= 0 && g_sync.clock >= at && town >= 0 && town < 6) {
            done_drop = true;
            SV_GRD_PART        *parts = SaveData->GetParts(town, &count);
            SV_EDIT_PARTS_INFO *plot = count > 0 ? SaveData->GetEditPartsInfo(town, parts[count - 1].part_id) : nullptr;

            if (plot != nullptr && plot->placed > 0) {
                std::fprintf(stderr, "worlds: test: this copy (apart, player %d) takes Georama part %d (plot %d) out of town %d at apart tick %lld\n",
                             Player(), town * 130 + count - 1, parts[count - 1].part_id, town, (long long) (g_state.split_tick + g_sync.clock));
                plot->placed--;
                parts[count - 1] = SV_GRD_PART{-1, -1, 0.0f, 0.0f, 0.0f};
            } else {
                std::fprintf(stderr, "worlds: test: town %d has no placed part to take out (%d parts)\n", town, count);
            }
            std::fflush(stderr);
        }
    }

    static const char *spec = TestEnv("DC_WORLDS_TEST_SLOT");
    // T24: DC_WORLDS_TEST_SLOT_AT_MERGE=REGION:INDEX:VALUE, the same change made as this copy starts the merge's link, so it
    // reaches the other copy while the merge comes up (after a catch-up host's checkpoint has gone: w-world-split -LateChange).
    static const char *at_merge = TestEnv("DC_WORLDS_TEST_SLOT_AT_MERGE");
    static long long   after = -1;
    static int         region = -1, index = -1, value = 0;
    static bool        parsed = false, done = false;

    if (!parsed) {
        parsed = true;
        if (at_merge != nullptr && std::sscanf(at_merge, "%d:%d:%d", &region, &index, &value) == 3) {
            after = 0;
        } else if (spec == nullptr || std::sscanf(spec, "%lld:%d:%d:%d", &after, &region, &index, &value) != 4) {
            after = -1;
        }
    }

    if (after < 0 || done || !g_state.apart || g_sync.clock < after || region < 0 || region >= kRegions || index < 0 ||
        index >= kRegionCount[region] || (at_merge != nullptr && !g_test_merge_started)) {
        return;
    }

    std::uint8_t bytes[kMaxBytes];
    const int    len = Read(region, index, bytes);

    if (len < 2) {
        return;
    }

    done = true;
    const std::int16_t v = (std::int16_t) value;

    std::memcpy(bytes, &v, 2);
    Write(region, index, bytes);
    std::fprintf(stderr, "worlds: test: this copy (apart, player %d) sets %s %d to %d at apart tick %lld\n", Player(), kRegionNames[region],
                 index, value, (long long) (g_state.split_tick + g_sync.clock));
    std::fflush(stderr);
}

} // namespace slotsync

// coop-small2: this copy's baseline of a live-sync slot set to what it holds now (the storage cell an answer of the host's wrote: no
// record of it goes back).
void SlotRebaseHere(int region, int index) {
    using namespace slotsync;
    std::uint8_t bytes[kMaxBytes];

    if (!g_sync.on || region < 0 || region >= kRegions || index < 0 || index >= (int) g_sync.baseline[region].size()) {
        return;
    }
    const int len = Read(region, index, bytes);

    g_sync.baseline[region][index].assign(bytes, bytes + len);
}

long long ClockMs();

// coop-flagsync: the kinds of story state a kWhatFlag carries (its c), and what each is called in the log.
enum FlagKind : std::int32_t {
    kFlagGame = 0,   // CSaveData::SetGameFlag (_SSET_GAME_FLAG): a flag
    kFlagInt = 1,    // CSaveData::SetGameIntFlag (_SSET_GAME_INT_FLAG): a flag, b value
    kFlagMap = 2,    // CSaveData::SetMapFlag (a town's or building's event points, chests: _SSET_MAP_FLAG): a flag, d map
    kFlagVisits = 3, // CSaveData::VisitMap (a map's visit count, a first visit's scene): a map, b count (the larger stands)
};

// coop-pt1111f: a kFlagMap's b is its value (bit 0) and, as kMapFlagInit, the sender's first-visit mark for that flag.
constexpr std::int32_t kMapFlagInit = 2;

const char *FlagName(std::int32_t kind, std::int32_t flag, std::int32_t map, char *out, std::size_t size) {
    if (kind == kFlagInt) {
        std::snprintf(out, size, "game int flag %d", flag);
    } else if (kind == kFlagMap) {
        std::snprintf(out, size, "map %d flag %d", map, flag);
    } else if (kind == kFlagVisits) {
        std::snprintf(out, size, "map %d visits", flag);
    } else {
        std::snprintf(out, size, "game flag %d", flag);
    }

    return out;
}

// A copy apart: its progress for the other copy, sent at once and then twice a second until acknowledged; both ways
// (Jep's 2026-10-21: an atla player 1 finds in a dungeon reaches player 2's copy at once, and hers his).
// DC_WORLDS_MUTATE=no_progress: none sent; no_progress_back: none from player 1 (the old one-way channel, twin).
void QueueProgress(std::int32_t what, std::int32_t a, std::int32_t b, std::int32_t c, std::int32_t d) {
    if (!On() || !g_state.apart || Player() == 0 || Mutated("no_progress") || (Player() == 1 && Mutated("no_progress_back"))) {
        return;
    }

    // coop-flagsync: a flag set again before the other copy acknowledged it goes once, with its newest value.
    if (what == kWhatFlag) {
        auto same = [&](const Progress &p) { return p.what == kWhatFlag && p.a == a && p.c == c && p.d == d; };

        g_state.progress_out.erase(std::remove_if(g_state.progress_out.begin(), g_state.progress_out.end(), same), g_state.progress_out.end());
    }

    Progress progress{kMagic, (std::uint32_t) Player(), kProgress, ++g_state.progress_id, what, a, b, c, d};

    g_state.progress_out.push_back(progress);
    g_state.progress_sent[progress.id] = progress; // coop-catchuprace (ApplyLateProgress)
    g_state.progress_due = std::chrono::steady_clock::now();
    if (what == kWhatFlag) {
        char name[48];

        const bool marked = c == kFlagMap && (b & kMapFlagInit) != 0;

        std::fprintf(stderr, "worlds: progress %u for player %d: %s = %d (clock %lld ms)%s\n", progress.id, 3 - Player(), FlagName(c, a, d, name, sizeof name),
                     c == kFlagMap ? b & 1 : b, ClockMs(), marked ? ", its first-visit mark with it" : "");
    } else if (what == kWhatParty) {
        std::fprintf(stderr, "worlds: progress %u for player %d: party of %d (clock %lld ms)\n", progress.id, 3 - Player(), a, ClockMs());
    } else if (what == kWhatGate) {
        std::fprintf(stderr, "worlds: progress %u for player %d: gate open in dungeon %d floor %d (clock %lld ms)\n", progress.id, 3 - Player(), c, a,
                     ClockMs());
    } else if (what == kWhatGain) {
        std::fprintf(stderr, "worlds: progress %u for player %d: gain %d of %d by %d (clock %lld ms)\n", progress.id, 3 - Player(), a, b, c, ClockMs());
    } else if (what == kWhatCall) {
        std::fprintf(stderr, "worlds: progress %u for player %d: a call to dungeon %d floor %d from floor %d (clock %lld ms)\n", progress.id, 3 - Player(), c,
                     a, d, ClockMs());
    } else if (what == kWhatDoorCall) {
        std::fprintf(stderr, "worlds: progress %u for player %d: a call from %s (clock %lld ms)\n", progress.id, 3 - Player(),
                     b != 0 ? "the cave door" : "a room's door", ClockMs());
    } else if (what == kWhatDoorAnswer) {
        std::fprintf(stderr, "worlds: progress %u for player %d: the door call's answer %d (clock %lld ms)\n", progress.id, 3 - Player(), c, ClockMs());
    } else {
        std::fprintf(stderr, "worlds: progress %u for player %d: %s %d %d (dungeon %d floor %d)\n", progress.id, 3 - Player(),
                     what == kWhatAtla ? "atla" : "floor", a, b, c, d);
    }
    std::fflush(stderr);
}

// coop-flagsync: this PC's steady clock in ms, the same on both copies of one PC (the tests' 2 s check across two logs).
long long ClockMs() {
    return (long long) std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void SendProgress(std::chrono::steady_clock::time_point now) {
    if (g_state.progress_out.empty() || now < g_state.progress_due || g_state.test_progress_held) {
        return;
    }

    g_state.progress_due = now + std::chrono::milliseconds(500);

    for (const Progress &progress : g_state.progress_out) {
        // DC_WORLDS_DROP_FIRST_PROGRESS=1: the first send is lost, as on a lossy link (a test's). coop-apartstats: =N, the first N
        // sends (the Atla's progress then lands after its town's slot record would have: w-town-apart -Case atla -AtlaLoss).
        static const int drop_sends = TestEnvInt("DC_WORLDS_DROP_FIRST_PROGRESS", 0);

        if (g_state.progress_dropped < drop_sends) {
            g_state.progress_dropped++;
            std::fprintf(stderr, "worlds: test: progress %u's first send dropped\n", progress.id);
            std::fflush(stderr);
            continue;
        }

        NetSocketSend(&progress, sizeof progress);
    }
}

// coop-joinatla: a dungeon's Atla as laid out in a status (its floors' slots), as a print and floor 1's count, for the log.
std::uint32_t AtlaPrint(const CUserStatus *status, int dungeon, int *floor1) {
    std::uint32_t print = 0x811c9dc5u;

    for (int floor = 0; floor < 40; floor++) {
        for (int slot = 0; slot < 8; slot++) {
            print = (print ^ (std::uint32_t) status->atra_grid[dungeon][floor][slot]) * 0x01000193u;
        }
    }
    *floor1 = ((CDngStatusData *) status)->GetMaxAtraNum(dungeon, 0);
    return print;
}

// coop-joinatla (Jep's 2026-10-28 run: "B1 shows 0/0 Atla and no Atla icons" on her floor menu after he went in first): the
// floor progress raised her dungeon's floor_reached from -1, so the game's own first entry there no longer laid out its
// Atla (BtAtraListMake runs only while it is -1) and every floor of hers had none. Laid out here instead, from the random
// state the other copy laid its own out from, so both copies hold the same Atla on the same floors; this copy's random
// state is put back. DC_WORLDS_MUTATE=atla_list_off (twin): not laid out, as before.
void AtlaListFromPeer(CUserStatus *status, int dungeon, unsigned rand) {
    for (int floor = 0; floor < 40; floor++) {
        if (((CDngStatusData *) status)->GetMaxAtraNum(dungeon, floor) != 0) {
            return; // laid out already (this copy's own entry)
        }
    }
    if (Mutated("atla_list_off")) {
        std::fprintf(stderr, "worlds: test: dungeon %d's Atla not laid out (atla_list_off)\n", dungeon);
        std::fflush(stderr);
        return;
    }

    CUserStatus   *was = UserStatus;
    const unsigned own = PortRandState();
    int            floor1 = 0;

    UserStatus = status; // in a town the save's dungeon status (BtAtraListMake writes through UserStatus)
    PortRandSetState(rand);
    BtAtraListMake(dungeon);
    PortRandSetState(own);
    UserStatus = was;

    // The floor menu, if it is open on this dungeon, counts again (InitDunEnterMenu counted them as it opened).
    if (DEnterMenu.dungeon == dungeon) {
        for (int floor = 0; floor < 40; floor++) {
            DEnterMenu.max_atra[floor] = (s8) ((CDngStatusData *) status)->GetMaxAtraNum(dungeon, floor);
            DEnterMenu.collected_atra[floor] = (s8) ((CDngStatusData *) status)->GetAtraNum(dungeon, floor);
        }
    }
    const std::uint32_t print = AtlaPrint(status, dungeon, &floor1);

    std::fprintf(stderr, "worlds: atla: dungeon %d's Atla laid out from the other copy's random state %08x: print %08x, floor 1 holds %d\n",
                 dungeon, rand, print, floor1);
    std::fflush(stderr);
}

// Player 2's progress (what, a, b, c, d) applied to this copy's save; what was done, for the log.
const char *ApplyProgress(const std::int32_t delta[5]) {
    static char line[160];

    if (delta[0] == kWhatCommand) { // coop-localmenu
        NetplayApplyRecordedCommand(delta);
        std::snprintf(line, sizeof line, "applied: command %d of player %d", (delta[1] >> 8) & 0xFF, delta[1] & 0xFF);
        return line;
    }

    if (delta[0] == kWhatAtla && SaveData != nullptr && (UserStatus != nullptr || SaveData->GetDngStatus() != nullptr)) {
        slotsync::Flush(); // coop-apartstats: this copy's own Georama changes go out first (its plots are refreshed from the save)
        AtlaToSave(delta[1], delta[2], SaveData, delta[3], delta[4]);
        for (int plot = 0; mode == GAME_MODE_EDIT && plot < 24; plot++) {
            CoopTownGeoramaPlotFromSave(delta[3], plot); // the dungeon's town, if this copy stands in it
        }
        slotsync::Settle(delta[3]); // coop-apartstats: not told back as a record
        std::snprintf(line, sizeof line, "applied: atla %d (slot %d) in dungeon %d floor %d", delta[1], delta[2], delta[3], delta[4]);
    } else if (delta[0] == kWhatFloor && delta[3] >= 0 && delta[3] < 7 &&
               (UserStatus != nullptr || (SaveData != nullptr && SaveData->GetDngStatus() != nullptr))) {
        CUserStatus *status = UserStatus != nullptr ? UserStatus : (CUserStatus *) SaveData->GetDngStatus();

        if (status->floor_reached[delta[3]] == -1 && delta[3] < 6 && delta[4] == 1) {
            AtlaListFromPeer(status, delta[3], (unsigned) delta[2]); // coop-joinatla
        }
        if (status->floor_reached[delta[3]] < delta[1]) {
            status->floor_reached[delta[3]] = (s8) delta[1];
        }
        // coop-flagsync: floors go both ways now, so what the other copy told of is not told back.
        if (g_state.floors_told[delta[3]] < status->floor_reached[delta[3]]) {
            g_state.floors_told[delta[3]] = status->floor_reached[delta[3]];
        }
        std::snprintf(line, sizeof line, "applied: floor %d reached in dungeon %d (now %d) (clock %lld ms)", delta[1], delta[3],
                      (int) status->floor_reached[delta[3]], ClockMs());
    } else if (delta[0] == kWhatFlag && SaveData != nullptr) {
        // coop-flagsync: set as the event's script did on the other copy (through the setter, which sends nothing back).
        char name[48];

        g_state.flag_from_peer = true;
        if (delta[3] == kFlagInt) {
            SaveData->SetGameIntFlag(delta[1], delta[2]);
        } else if (delta[3] == kFlagMap) {
            if ((delta[2] & kMapFlagInit) != 0) {
                SaveData->SetMapInitFlag(delta[4], delta[1], 1); // coop-pt1111f: this copy's load of the room keeps the flag
            }
            SaveData->SetMapFlag(delta[4], delta[1], delta[2] & 1);
        } else if (delta[3] == kFlagVisits) {
            const int now = SaveData->VisitMap(delta[1], 0);

            if (delta[2] > now) {
                SaveData->VisitMap(delta[1], delta[2] - now); // the larger count stands: her visits are his too
            }
        } else {
            SaveData->SetGameFlag(delta[1], delta[2]);
        }
        g_state.flag_from_peer = false;
        const bool marked = delta[3] == kFlagMap && (delta[2] & kMapFlagInit) != 0;

        std::snprintf(line, sizeof line, "applied: %s = %d (clock %lld ms)%s", FlagName(delta[3], delta[1], delta[4], name, sizeof name),
                      delta[3] == kFlagMap ? delta[2] & 1 : delta[2], ClockMs(), marked ? ", its first-visit mark with it" : "");
    } else if (delta[0] == kWhatParty && (UserStatus != nullptr || (SaveData != nullptr && SaveData->GetDngStatus() != nullptr))) {
        // coop-apartsync: the character who joined on the other copy joins here too (the larger party stands).
        CUserStatus *status = UserStatus != nullptr ? UserStatus : (CUserStatus *) SaveData->GetDngStatus();

        if (status->party_size < delta[1] && delta[1] <= 6) {
            status->party_size = (s8) delta[1];
        }
        g_state.party_told = std::max<int>(g_state.party_told, status->party_size); // not told back
        std::snprintf(line, sizeof line, "applied: party of %d (now %d) (clock %lld ms)", delta[1], (int) status->party_size, ClockMs());
    } else if (delta[0] == kWhatGate) {
        // coop-gateopen: the gate the other player opened stays open here (its floor, built now or later).
        coop::GateOpenFromPeer(delta[3], delta[1]);
        std::snprintf(line, sizeof line, "applied: gate open in dungeon %d floor %d (clock %lld ms)", delta[3], delta[1], ClockMs());
    } else if (delta[0] == kWhatSlot) {
        return slotsync::Replay((std::uint32_t) delta[1]);
    } else if (delta[0] == kWhatLoaded) {
        // coop-saveload: the merge takes both copies to his. coop-loadpull: at once, wherever this copy is (MergeReady).
        g_state.load_pull = !Mutated("load_no_pull");
        std::snprintf(line, sizeof line, "applied: player 1 loaded a save (file %d); %s", delta[1],
                      g_state.load_pull ? "this copy merges into his loaded state, where he is" : "the merge takes his");
    } else if (delta[0] == kWhatGain || delta[0] == kWhatNow) {
        return statsync::Apply(delta); // coop-apartstats
    } else if (delta[0] == kWhatCall) {
        std::snprintf(line, sizeof line, "applied: a call to dungeon %d floor %d (a question for this player)", delta[3], delta[1]);
    } else if (delta[0] == kWhatCallAnswer) {
        std::snprintf(line, sizeof line, "applied: the call to dungeon %d floor %d answered %s", delta[3], delta[1],
                      delta[2] == 1 ? "coming" : delta[2] == 2 ? "busy" : "not now");
    } else if (delta[0] == kWhatDoorCall) {
        std::snprintf(line, sizeof line, "applied: a call from %s (a notice only)", delta[2] != 0 ? "the cave door" : "a room's door");
    } else if (delta[0] == kWhatDoorAnswer) {
        std::snprintf(line, sizeof line, "applied: the door call's answer %d (a note only)", delta[3]);
    } else if (delta[0] == kWhatStand) {
        coop::SpawnSetStand(coop::SpawnUnpack(delta), "the catch-up's recording");
        std::snprintf(line, sizeof line, "applied: player 2's stand for the merge");
    } else {
        std::snprintf(line, sizeof line, "nothing to apply (what %d)", delta[0]);
    }

    return line;
}

// Either copy: apply the other's progress once (each world is alone while apart, so nothing is in lockstep), then ack.
void TakeProgress(const Progress &progress) {
    // coop-catchuprace: her progress that reaches the host after its catch-up's last blob (it names J), or after its merge tick,
    // rides no blob to her replay: it goes by the lockstep (below), not dropped once he has merged (she took his state at the
    // lockstep, so dropped it was lost on both, and her copy resent it unacknowledged for good).
    // DC_WORLDS_MUTATE=late_progress_direct (twin): applied at once (or dropped once merged), as before.
    const bool late = Player() == 1 && progress.player == 2 && NetplayCatchLastBlobGone() && !Mutated("late_progress_direct");

    if (Player() == 0 || (!g_state.apart && !late) || progress.player != (std::uint32_t) (3 - Player())) {
        return; // only apart; a merge waits until neither copy has anything unacknowledged
    }

    // coop-flagsync: a flag's older value that lands after a newer one is acknowledged and dropped.
    bool stale = false;

    if (progress.what == kWhatFlag) {
        std::uint32_t &newest = g_state.flag_applied[progress.c * 0x1000000 + progress.d * 0x10000 + progress.a];

        stale = progress.id < newest;
        newest = std::max(newest, progress.id);
    }

    // coop-loadpull: on his copy, what she did apart reaches him after his load: the loaded save is what both go on from, so
    // it is acknowledged and dropped (her copy takes his state at the merge anyway).
    if (!stale && Player() == 1 && g_state.load_pull && !Mutated("load_no_pull") && g_state.progress_applied.insert(progress.id).second) {
        std::fprintf(stderr, "worlds: progress %u from player 2 dropped: player 1 loaded a save (what %d)\n", progress.id, progress.what);
        stale = true;
    }

    // coop-catchuprace (the slot records' race, 8201e1b6): applied here at once, after his last blob, it was in his play from
    // that moment and in no blob, so her replay never had it: OUT OF SYNC at the next checksum. Now it goes into the lockstep as
    // his command and both copies apply it on that command's tick (hers from her own, progress_sent). Nobody waits: the command
    // rides his next input frame, and it is acknowledged at once. A call or a door call is a question, no state: as before.
    const bool state = progress.what != kWhatCall && progress.what != kWhatCallAnswer && progress.what != kWhatDoorCall && progress.what != kWhatDoorAnswer;
    const bool by_lockstep = !stale && late && state;

    if (by_lockstep && g_state.progress_applied.insert(progress.id).second) {
        NetCommand command;

        command.kind = kCmdProgress;
        command.c = progress.id;
        g_state.progress_late[progress.id] = progress;
        std::fprintf(stderr, "worlds: progress %u from player 2 (what %d) came after the catch-up's last blob: it goes by the lockstep\n", progress.id,
                     progress.what);
        std::fflush(stderr);
        NetplayQueueCommand(command);
    } else if (!by_lockstep && !stale && g_state.progress_applied.insert(progress.id).second) {
        char               line[200];
        const std::int32_t delta[5] = {progress.what, progress.a, progress.b, progress.c, progress.d};

        std::snprintf(line, sizeof line, "progress %u from player %u %s", progress.id, progress.player, ApplyProgress(delta));
        Log(line);

        if (Player() == 1 && progress.what != kWhatCall && progress.what != kWhatCallAnswer && progress.what != kWhatDoorCall &&
            progress.what != kWhatDoorAnswer) {
            NetplayCatchupDelta(delta); // a catch-up's recording keeps it, so a joiner's replay has it too
        }

        // coop-floorgate: the other player's gate to a floor that needs both did not open: this player is asked to come (CallTick).
        // DC_WORLDS_MUTATE=gather_no_call (twin): never asked.
        // coop-gatepopup: b 1, the caller withdrew it (its Circle): the question closes here, its player told.
        if (progress.what == kWhatCall && progress.b == 1) {
            CallWithdrawn((int) progress.player);
        } else if (progress.what == kWhatCall && !Mutated("gather_no_call")) {
            CallReceived((int) progress.player, progress.c, progress.a, progress.d);
        }

        if (progress.what == kWhatCallAnswer) {
            CallAnswered(progress.c, progress.a, progress.b);
        }

        // coop-doorgate: the other player's door that needs both did not open: come there. DC_WORLDS_MUTATE=door_no_call (twin):
        // no notice.
        // Asked in the game's window on this copy (DC_WORLDS_MUTATE=door_question_hud: a notice, as first built).
        if (progress.what == kWhatDoorCall && progress.c == 1) {
            doorgate::Withdrawn((int) progress.player); // coop-gatepopup: the caller's Circle
        } else if (progress.what == kWhatDoorCall && !Mutated("door_no_call")) {
            if (Mutated("door_question_hud")) {
                const char *notice = doorgate::CallText((int) progress.player, progress.a, progress.b != 0);

                NetplayShowNotice(notice);
                std::fprintf(stderr, "worlds: doorgate: call on this copy (player %d): \"%s\"\n", Player(), notice);
                std::fflush(stderr);
            } else {
                doorgate::AskApart((int) progress.player, progress.a, progress.b != 0);
            }
        }
        if (progress.what == kWhatDoorAnswer) {
            doorgate::Answered((int) progress.player, progress.a, progress.b != 0, progress.c);
        }

        if (progress.what == kWhatAtla) {
            char notice[64];

            std::snprintf(notice, sizeof notice, "Player %u found an Atla", progress.player);
            NetplayShowNotice(notice);
            slotsync::BagDump("atla applied"); // coop-apartstats (DC_WORLDS_TEST_ATLADUMP)
        }
        // coop-saveload: DC_COOP_MUTATE=saveload_nonotice (twin): no notice.
        if (progress.what == kWhatLoaded && !coop::Mutated("saveload_nonotice")) {
            NetplayShowNotice("Player 1 loaded a save");
            std::fprintf(stderr, "coop-saveload: notice on player 2's copy (apart): player 1 loaded a save\n");
        }
    }

    Progress ack{kMagic, (std::uint32_t) Player(), kProgressAck, progress.id, 0, 0, 0, 0, 0};

    NetSocketSend(&ack, sizeof ack);
}

// coop-catchuprace: kCmdProgress applied, on both copies on one lockstep tick: the host takes her progress as TakeProgress would
// have, her copy applies its own again (made before the checkpoint replaced her state). Neither records it for a catch-up.
void ApplyLateProgress(int player, const NetCommand &command, std::int64_t tick) {
    const std::uint32_t id = command.c;
    auto               &held = Player() == 1 ? g_state.progress_late : g_state.progress_sent;
    auto                it = held.find(id);

    if (it == held.end()) {
        std::fprintf(stderr, "worlds: late progress %u not held at tick %lld\n", id, (long long) tick);
        std::fflush(stderr);
        return;
    }

    const Progress     progress = it->second;
    const std::int32_t delta[5] = {progress.what, progress.a, progress.b, progress.c, progress.d};

    if (Player() == 1) {
        held.erase(it);
    }
    std::fprintf(stderr, "worlds: late progress %u applied by the lockstep at tick %lld (player %d's command): %s\n", id, (long long) tick, player,
                 ApplyProgress(delta));
    std::fflush(stderr);
}

// coop-apartstats (Jep, 2026-10-28: what she gains apart is lost at the rejoin): each character's own stats and the floors'
// kills while the two play apart. A merge puts the state sender's copy over the other's, and none of these were told apart:
// her Fruit of Eden and Gourd (max life, the water gauge), a favourite food's defense, the kills on her floors, and her life,
// water and ailments as she came back (his old copy of them stood). Two kinds:
// - lasting gains add up: each change goes to the other copy as a progress (kWhatGain, by how much; resent until acknowledged,
//   so a merge waits for it, MergeReady) and the other adds it, both ways.
// - life, water and ailments now belong to whoever played that character apart: each copy tells the other its characters'
//   with every presence (NowMsg), and as a merge starts (CoopWorldsMayLink) both copies put in the other's for the characters
//   only the other changed (both changed one: the copy playing it now; both or neither playing it: player 1's). Whichever
//   copy sends the merge's state then has them, and the host's catch-up recording (kWhatNow) puts them in on the joiner's
//   replay on the same tick, as StandForMerge's stand.
// DC_WORLDS_MUTATE=no_stat_sync (twin): no gains told; no_chara_now (twin): the merge takes the sender's characters as before.
namespace statsync {

enum Gain : std::int32_t {
    kGainMaxHp = 1,   // CUserStatus::max_hp (Fruit of Eden)
    kGainWater = 2,   // water_max (a Gourd; by a float's bits)
    kGainDefense = 3, // defense (a favourite food)
    kGainKills = 4,   // kills (b: dungeon * 100 + floor)
};

const char *GainName(int gain) {
    return gain == kGainMaxHp ? "max life" : gain == kGainWater ? "water gauge" : gain == kGainDefense ? "defense" : gain == kGainKills ? "kills" : "?";
}

#pragma pack(push, 1)
// A character's life, water and ailments now (the gauges' steps with them: the game moves life and water towards next_hp and
// drink_next each step, so those go together).
struct Now {
    s16   hp, next_hp, life_step, ailment_frames, drink_next, drink_step;
    s32   ailments;
    float water_now, thirst_damage;
};

struct NowMsg {
    std::uint32_t magic;
    std::uint32_t player;
    std::uint32_t kind; // kCharaNow
    std::uint32_t seq;
    std::int32_t  chara;   // the member the sender plays now
    std::uint32_t changed; // the characters whose Now changed on the sender since the split (a bit each)
    std::uint32_t loaded;  // the sender loaded a save apart (coop-saveload: the merge takes player 1's as they are)
    Now           now[6];
};
#pragma pack(pop)

static_assert(sizeof(NowMsg) != sizeof(Message) && sizeof(NowMsg) != sizeof(Progress) && sizeof(NowMsg) != sizeof(SceneView) &&
                  sizeof(NowMsg) != sizeof(slotsync::SlotMsg) && sizeof(NowMsg) != kCoopWorldsScenePacksBytes,
              "worlds messages are told apart by their size");

struct Sync {
    bool          set = false;
    s16           max_hp[6] = {}; // the lasting stats as last counted (a change from these is a gain)
    float         water_max[6] = {};
    s32           defense[6] = {};
    s16           kills[6][100] = {};
    Now           split[6] = {}; // each character's Now as both had it at the split (or as the merge put it in)
    std::uint32_t changed = 0;
    bool          loaded = false;
    std::uint32_t seq = 0;
    bool          peer_set = false;
    NowMsg        peer{};
    long long     ticks = 0; // ticks of play apart (the test hook)
};

Sync g_stats;

Now Read(const CUserStatus *party, int c) {
    return Now{party->hp[c],         party->next_hp[c],    party->life_step[c], party->ailment_frames[c], party->drink_next[c],
               party->drink_step[c], party->ailments[c],   party->water_now[c], party->thirst_damage[c]};
}

void Write(CUserStatus *party, int c, const Now &now) {
    party->hp[c] = now.hp;
    party->next_hp[c] = now.next_hp;
    party->life_step[c] = now.life_step;
    party->ailment_frames[c] = now.ailment_frames;
    party->drink_next[c] = now.drink_next;
    party->drink_step[c] = now.drink_step;
    party->ailments[c] = now.ailments;
    party->water_now[c] = now.water_now;
    party->thirst_damage[c] = now.thirst_damage;
}

bool Same(const Now &a, const Now &b) {
    return std::memcmp(&a, &b, sizeof a) == 0;
}

// The lasting stats as this copy holds them now: what the next gain is counted from.
void Rebase() {
    CUserStatus *party = slotsync::Party();

    if (party == nullptr) {
        return;
    }
    std::memcpy(g_stats.max_hp, party->max_hp, sizeof g_stats.max_hp);
    std::memcpy(g_stats.water_max, party->water_max, sizeof g_stats.water_max);
    std::memcpy(g_stats.defense, party->defense, sizeof g_stats.defense);
    std::memcpy(g_stats.kills, party->kills, sizeof g_stats.kills);
}

// At the split (slotsync::Begin, every kind of it): what both copies hold now.
void Begin() {
    CUserStatus *party = slotsync::Party();

    g_stats = Sync{};
    if (party == nullptr) {
        return; // the first tick apart with a party counts from there
    }
    Rebase();
    for (int c = 0; c < 6; c++) {
        g_stats.split[c] = Read(party, c);
    }
    g_stats.set = true;
}

// coop-saveload: player 1 loaded a save apart: what it holds now is the save's, not gains (the merge takes his).
void Loaded() {
    g_stats.loaded = true;
    Rebase();
}

void Gain(int gain, int index, int by, const char *by_text) {
    std::fprintf(stderr, "worlds: stats: %s of %d changed here by %s (player %d, apart; clock %lld ms)\n", GainName(gain), index, by_text, Player(), ClockMs());
    if (!Mutated("no_stat_sync")) {
        QueueProgress(kWhatGain, gain, index, by, 0);
    }
}

void TestHook(CUserStatus *party);

// Each tick apart: this copy's gains to the other, and which characters' Now changed here.
void Tick() {
    CUserStatus *party = slotsync::Party();

    if (!g_state.apart || Player() == 0 || party == nullptr) {
        return;
    }
    if (!g_stats.set) {
        Begin();
        return;
    }
    // A merge's state coming in (a catch-up's replay, a floor snapshot, the host's state taken) is the other copy's, not play
    // here; after a load apart the save's.
    if (NetplayCatchUnderWay() || NetplaySnapshotApplied() || g_merge_state_in || g_stats.loaded) {
        Rebase();
        return;
    }

    g_stats.ticks++;
    TestHook(party);

    char by[32];

    for (int c = 0; c < 6; c++) {
        if (party->max_hp[c] != g_stats.max_hp[c]) {
            const int d = party->max_hp[c] - g_stats.max_hp[c];

            g_stats.max_hp[c] = party->max_hp[c];
            std::snprintf(by, sizeof by, "%d", d);
            Gain(kGainMaxHp, c, d, by);
        }
        if (party->water_max[c] != g_stats.water_max[c]) {
            const float  d = party->water_max[c] - g_stats.water_max[c];
            std::int32_t bits;

            std::memcpy(&bits, &d, 4);
            g_stats.water_max[c] = party->water_max[c];
            std::snprintf(by, sizeof by, "%.1f", d);
            Gain(kGainWater, c, bits, by);
        }
        if (party->defense[c] != g_stats.defense[c]) {
            const int d = party->defense[c] - g_stats.defense[c];

            g_stats.defense[c] = party->defense[c];
            std::snprintf(by, sizeof by, "%d", d);
            Gain(kGainDefense, c, d, by);
        }
        if (!Same(Read(party, c), g_stats.split[c])) {
            g_stats.changed |= 1u << c;
        }
    }

    for (int d = 0; d < 6; d++) {
        for (int f = 0; f < 100; f++) {
            if (party->kills[d][f] != g_stats.kills[d][f]) {
                const int n = party->kills[d][f] - g_stats.kills[d][f];

                g_stats.kills[d][f] = party->kills[d][f];
                std::snprintf(by, sizeof by, "%d", n);
                Gain(kGainKills, d * 100 + f, n, by);
            }
        }
    }
}

// With each presence: this copy's characters now.
void Send() {
    CUserStatus *party = slotsync::Party();

    if (!g_stats.set || party == nullptr || Player() == 0) {
        return;
    }

    NowMsg msg{};

    msg.magic = kMagic;
    msg.player = (std::uint32_t) Player();
    msg.kind = kCharaNow;
    msg.seq = ++g_stats.seq;
    msg.chara = party->cur_chara;
    msg.changed = g_stats.changed;
    msg.loaded = g_stats.loaded ? 1u : 0u;
    for (int c = 0; c < 6; c++) {
        msg.now[c] = Read(party, c);
    }
    NetSocketSend(&msg, sizeof msg);
}

bool Receive(const void *data, int size) {
    if (size != (int) sizeof(NowMsg)) {
        return false;
    }

    NowMsg msg;

    std::memcpy(&msg, data, sizeof msg);
    if (msg.kind == kCharaNow && msg.player == (std::uint32_t) (3 - Player()) && (g_state.apart || g_state.merge_pending) &&
        (!g_stats.peer_set || msg.seq > g_stats.peer.seq)) {
        g_stats.peer = msg;
        g_stats.peer_set = true;
    }
    return true;
}

// The merge waits for a word of the other's characters (its next presence brings one).
bool PeerHeard() {
    return g_stats.peer_set || Mutated("no_chara_now");
}

// A catch-up recording's words for a character's Now (part 0: life, its step, ailments; part 1: water, its step, thirst).
void Pack(int c, int part, const Now &now, std::int32_t delta[5]) {
    auto pair = [](s16 lo, s16 hi) { return (std::int32_t) ((std::uint32_t) (std::uint16_t) lo | ((std::uint32_t) (std::uint16_t) hi << 16)); };

    delta[0] = kWhatNow;
    delta[1] = c | (part << 8);
    if (part == 0) {
        delta[2] = pair(now.hp, now.next_hp);
        delta[3] = pair(now.life_step, now.ailment_frames);
        delta[4] = now.ailments;
    } else {
        std::memcpy(&delta[2], &now.water_now, 4);
        delta[3] = pair(now.drink_next, now.drink_step);
        std::memcpy(&delta[4], &now.thirst_damage, 4);
    }
}

// As a merge starts, on both copies: the other's characters for those only it played apart.
void Merge() {
    CUserStatus *party = slotsync::Party();
    char         line[240];

    if (party == nullptr || !g_stats.set) {
        return;
    }
    if (Mutated("no_chara_now")) {
        Log("stats: the merge takes the sender's characters as they are (no_chara_now)");
        return;
    }
    if (!g_stats.peer_set) {
        Log("stats: no word of the other copy's characters: the merge takes the sender's");
        return;
    }
    if (g_stats.loaded || g_stats.peer.loaded != 0) {
        Log("stats: player 1 loaded a save apart: the merge takes his characters as they are");
        return;
    }

    const int here = party->cur_chara;
    const int there = g_stats.peer.chara;

    for (int c = 0; c < 6; c++) {
        if ((g_stats.peer.changed >> c & 1u) == 0) {
            continue;
        }

        const bool both = (g_stats.changed >> c & 1u) != 0;
        const bool take = !both || (there == c && here != c) || ((there == c) == (here == c) && Player() == 2);

        if (!take) {
            std::snprintf(line, sizeof line, "stats: character %d changed on both copies apart: this copy's stands (it plays %d, player %d plays %d)", c, here,
                          3 - Player(), there);
            Log(line);
            continue;
        }

        const Now &now = g_stats.peer.now[c];

        Write(party, c, now);
        g_stats.split[c] = now; // put in, not changed here
        g_stats.changed &= ~(1u << c);
        for (int part = 0; part < 2; part++) {
            std::int32_t delta[5];

            Pack(c, part, now, delta);
            NetplayCatchupDelta(delta); // a catch-up joiner's replay puts them in on the same tick
        }
        std::snprintf(line, sizeof line, "stats: character %d's life %d (to %d), water %.1f, ailments 0x%x from player %d (it played them apart)%s", c,
                      (int) now.hp, (int) now.next_hp, now.water_now, (unsigned) now.ailments, 3 - Player(), both ? " (both changed: it plays them)" : "");
        Log(line);
    }
}

// The other copy's gain (kWhatGain), or a catch-up recording's Now (kWhatNow), applied here; what was done, for the log.
const char *Apply(const std::int32_t delta[5]) {
    static char  line[160];
    CUserStatus *party = slotsync::Party();

    if (party == nullptr) {
        std::snprintf(line, sizeof line, "nothing to apply it to (no party)");
        return line;
    }

    if (delta[0] == kWhatNow) {
        const int c = delta[1] & 0xFF;
        const int part = delta[1] >> 8;

        if (c < 0 || c >= 6) {
            std::snprintf(line, sizeof line, "nothing to apply (character %d)", c);
            return line;
        }

        Now now = Read(party, c);

        if (part == 0) {
            now.hp = (s16) (delta[2] & 0xFFFF);
            now.next_hp = (s16) ((std::uint32_t) delta[2] >> 16);
            now.life_step = (s16) (delta[3] & 0xFFFF);
            now.ailment_frames = (s16) ((std::uint32_t) delta[3] >> 16);
            now.ailments = delta[4];
        } else {
            std::memcpy(&now.water_now, &delta[2], 4);
            now.drink_next = (s16) (delta[3] & 0xFFFF);
            now.drink_step = (s16) ((std::uint32_t) delta[3] >> 16);
            std::memcpy(&now.thirst_damage, &delta[4], 4);
        }
        Write(party, c, now);
        std::snprintf(line, sizeof line, "applied: character %d's %s from player 2's play apart", c, part == 0 ? "life and ailments" : "water");
        return line;
    }

    const int gain = delta[1];
    const int index = delta[2];
    const int by = delta[3];

    if (gain == kGainMaxHp && index >= 0 && index < 6) {
        static const int limit[6] = {0xAA, 0x8C, 0xAA, 0x8C, 0xB4, 0xA0}; // a Fruit of Eden's (menu_misc's ITEM_FRUIT_OF_EDEN)
        const int        was = party->max_hp[index];
        const int        now = std::max(1, std::min(was + by, std::max(limit[index], was)));

        party->max_hp[index] = (s16) now;
        g_stats.max_hp[index] = (s16) (g_stats.max_hp[index] + (now - was)); // not told back
        std::snprintf(line, sizeof line, "applied: max life of character %d by %d (now %d) (clock %lld ms)", index, by, now, ClockMs());
    } else if (gain == kGainWater && index >= 0 && index < 6) {
        // A Gourd fills a gauge of up to 90 by its volume; 100 here (inference: the game's own cap is the Gourd's own check).
        float       d;
        const float was = party->water_max[index];

        std::memcpy(&d, &by, 4);
        const float now = std::max(0.0f, std::min(was + d, std::max(100.0f, was)));

        party->water_max[index] = now;
        g_stats.water_max[index] += now - was;
        std::snprintf(line, sizeof line, "applied: water gauge of character %d by %.1f (now %.1f) (clock %lld ms)", index, d, now, ClockMs());
    } else if (gain == kGainDefense && index >= 0 && index < 6) {
        const int was = party->defense[index];
        const int now = std::max(0, std::min(was + by, std::max(99, was))); // a favourite food's cap (menu_misc)

        party->defense[index] = now;
        g_stats.defense[index] += now - was;
        std::snprintf(line, sizeof line, "applied: defense of character %d by %d (now %d) (clock %lld ms)", index, by, now, ClockMs());
    } else if (gain == kGainKills && index >= 0 && index < 600) {
        s16      &kills = party->kills[index / 100][index % 100];
        const int was = kills;
        const int now = std::max(0, std::min(was + by, 0x7FFF));

        kills = (s16) now;
        g_stats.kills[index / 100][index % 100] = (s16) (g_stats.kills[index / 100][index % 100] + (now - was));
        std::snprintf(line, sizeof line, "applied: kills in dungeon %d floor %d by %d (now %d) (clock %lld ms)", index / 100, index % 100, by, now, ClockMs());
    } else {
        std::snprintf(line, sizeof line, "nothing to apply (gain %d of %d)", gain, index);
    }
    return line;
}

// Test hook DC_WORLDS_TEST_STATS=K:C:MAX:WATER:LIFE:AIL:F:N (w-apartsync -Case stats): K ticks of play apart, this copy's
// character C gains MAX max life and WATER of water gauge (a Fruit of Eden and a Gourd), its life is LIFE (its gauge still)
// and its ailments AIL, and dungeon 0's floor F counts N more kills.
void TestHook(CUserStatus *party) {
    static const char *const spec = TestEnv("DC_WORLDS_TEST_STATS");
    static bool              done = false;
    long long                k = 0;
    int                      c = 0, max = 0, water = 0, life = 0, ail = 0, f = 0, n = 0;

    if (done || spec == nullptr || std::sscanf(spec, "%lld:%d:%d:%d:%d:%d:%d:%d", &k, &c, &max, &water, &life, &ail, &f, &n) != 8 || g_stats.ticks < k) {
        return;
    }
    done = true;
    if (c < 0 || c >= 6 || f < 0 || f >= 100) {
        return;
    }
    party->max_hp[c] = (s16) (party->max_hp[c] + max);
    party->water_max[c] += (float) water;
    if (life > 0) {
        party->hp[c] = party->next_hp[c] = (s16) life;
        party->life_step[c] = 0;
    }
    if (ail >= 0) {
        party->ailments[c] = ail;
    }
    party->kills[0][f] = (s16) (party->kills[0][f] + n);
    std::fprintf(stderr, "worlds: test: this copy (apart, player %d) changes character %d: max life +%d, water gauge +%d, life %d, ailments %d, "
                         "dungeon 0 floor %d kills +%d (clock %lld ms)\n",
                 Player(), c, max, water, life, ail, f, n, ClockMs());
    Dump("test stats");
}

// Test hook DC_WORLDS_TEST_STATDUMP=C: character C's stats and dungeon 0's first floors' kills, with each bag dump's label.
void Dump(const char *label) {
    static const int   c = TestEnvInt("DC_WORLDS_TEST_STATDUMP", -1);
    const CUserStatus *party = slotsync::Party();

    if (c < 0 || c >= 6 || party == nullptr) {
        return;
    }
    std::fprintf(stderr, "worlds: statdump %s (player %d, %s): chara %d life %d next %d max %d water %.1f of %.1f ailments %d defense %d; kills %d %d %d\n",
                 label, Player(), g_state.apart ? "apart" : "together", c, (int) party->hp[c], (int) party->next_hp[c], (int) party->max_hp[c],
                 party->water_now[c], party->water_max[c], (int) party->ailments[c], (int) party->defense[c], (int) party->kills[0][0],
                 (int) party->kills[0][1], (int) party->kills[0][2]);
    std::fflush(stderr);
}

} // namespace statsync

} // namespace

// coop-small2: (the anonymous namespace's SlotRebaseHere, for coop_storage.cpp)
void CoopWorldsSlotRebase(int region, int index) {
    SlotRebaseHere(region, index);
}

void CoopWorldsApplyProgress(const std::int32_t delta[5]) {
    std::fprintf(stderr, "worlds: catch-up: player 2's progress from apart %s\n", ApplyProgress(delta));
    std::fflush(stderr);
}

// The game's own getAtraToSaveData, and player 2's copy apart tells the host.
namespace dc::game { // repin: was PORT_OVERRIDE; a recipe removes the game's own definition
void getAtraToSaveData(int atra, int atra_no, CSaveData *save, int dungeon, int floor) {
    std::printf("GET ATRA [%d] !!\n", atra);
    // coop-apartstats: the progress alone takes it to the other copy (slotsync::Settle), this copy's earlier changes first.
    const bool live = save == SaveData;

    if (live) {
        slotsync::Flush();
    }
    AtlaToSave(atra, atra_no, save, dungeon, floor);
    if (live) {
        slotsync::Settle(dungeon);
    }
    QueueProgress(kWhatAtla, atra, atra_no, dungeon, floor);
}
} // namespace dc::game

namespace {

// coop-flagsync: a game flag this copy's own events changed while apart goes to the other copy, so the event that set it
// (a cutscene, a talk) is done on both before they meet again (Jep's 2026-10-25 note 19: "P1 goes into the mayors house
// after P2 watches the cutscene, P1 still has to watch the cutscene"). Not one the other copy sent (flag_from_peer), and
// only the live save's. DC_WORLDS_MUTATE=no_flag_sync (twin): none sent, as before.
void FlagChanged(const CSaveData *save, int kind, int flag, int before, int after, int map = 0) {
    // DC_WORLDS_TEST_WATCH_FLAG=F (the test's): each time this copy's own scripts set game flag F (a scene that marks itself
    // seen sets it as it plays), with where this copy is; "all": every set of every kind (map flags, visits too), one that
    // leaves it as it was among them (a scene played again sets its flag again).
    static const char *const watch_spec = TestEnv("DC_WORLDS_TEST_WATCH_FLAG");
    static const bool        watch_all = watch_spec != nullptr && std::strcmp(watch_spec, "all") == 0;
    static const int         watch = watch_spec != nullptr && !watch_all ? std::atoi(watch_spec) : -1;

    if (save == SaveData && !g_state.flag_from_peer && ((kind == kFlagGame && flag == watch) || watch_all)) {
        char where[96];
        char name[48];

        Describe(Here(), where, sizeof where);
        std::fprintf(stderr, "worlds: test: this copy's script (player %d) sets %s to %d (was %d), %s, %s (clock %lld ms)\n", Player(),
                     FlagName(kind, flag, map, name, sizeof name), after, before, where,
                     NetplayCatchingUp() ? "catching up" : g_state.apart ? "apart" : "together", ClockMs());
        std::fflush(stderr);
    }

    if (save != SaveData || before == after || g_state.flag_from_peer || g_flags_local || Mutated("no_flag_sync")) {
        return;
    }

    // coop-pt1111f (Jep's 2026-11-11 playtest item 17: apart, he opened the mayor's red chest, went out to her town and in
    // again, and the chest was shut and full once more): a map flag goes with this copy's first-visit mark for it
    // (CSaveData::map_init_flags, as kMapFlagInit in b). A room's load (edit_in.cpp; LoadPTS for a town's parts) gives each of
    // its points' flags its starting value once, while that mark is clear, and sets the mark. Apart his copy set the mark and
    // the other copy never did; the merge went by her state (she stayed in town), so his next load of the room found the mark
    // clear and put the chest back shut over the opened flag his progress had carried (it could be looted again).
    // DC_WORLDS_MUTATE=map_init_unsent (twin): the flag alone, as before.
    int value = after;

    if (kind == kFlagMap && const_cast<CSaveData *>(save)->GetMapInitFlag(map, flag) != 0 && !Mutated("map_init_unsent")) {
        value |= kMapFlagInit;
    }

    QueueProgress(kWhatFlag, flag, value, kind, map);
}

} // namespace

// The game's own CSaveData::SetGameFlag (256 words of bits; a flag out of range is no flag), and an apart copy tells the other.
namespace dc::game { // repin: was PORT_OVERRIDE; a recipe removes the game's own definition
int CSaveData::SetGameFlag(int flag_no, int value) {
    if (flag_no < 0 || (flag_no >> 5) >= 256) {
        return 0;
    }

    const u32 bit = 1u << (flag_no & 31);
    const int before = (this->game_flags[flag_no >> 5] & bit) != 0;

    if (value) {
        this->game_flags[flag_no >> 5] |= bit;
    } else {
        this->game_flags[flag_no >> 5] &= ~bit;
    }

    FlagChanged(this, 0, flag_no, before, value ? 1 : 0);
    return value;
}
} // namespace dc::game

// The game's own CSaveData::SetMapFlag (80 maps of 16 words of bits: a town's or building's event points, its chests),
// likewise.
namespace dc::game { // repin: was PORT_OVERRIDE; a recipe removes the game's own definition
int CSaveData::SetMapFlag(int map_no, int flag_no, int value) {
    if (map_no < 0 || map_no >= 0x50 || flag_no < 0 || (flag_no >> 5) >= 16) {
        return 0;
    }

    const u32 bit = 1u << (flag_no & 31);
    const int before = (this->map_flags[map_no][flag_no >> 5] & bit) != 0;

    if (value) {
        this->map_flags[map_no][flag_no >> 5] |= bit;
    } else {
        this->map_flags[map_no][flag_no >> 5] &= ~bit;
    }

    FlagChanged(this, kFlagMap, flag_no, before, value ? 1 : 0, map_no);
    return value;
}
} // namespace dc::game

// The game's own CSaveData::VisitMap (a map's visits, at most 9999; add 0 only reads), likewise: the count it has now.
namespace dc::game { // repin: was PORT_OVERRIDE; a recipe removes the game's own definition
int CSaveData::VisitMap(int map_no, int add) {
    if (map_no < 0 || map_no >= 0x50) {
        return 0;
    }

    const int before = this->visit_map[map_no];

    this->visit_map[map_no] = (s16) std::min(before + add, 0x270F);
    if (add != 0) {
        FlagChanged(this, kFlagVisits, map_no, before, this->visit_map[map_no]);
    }

    return this->visit_map[map_no];
}
} // namespace dc::game

// The game's own CEditGround::StepWater (each town water surface in view gets its ripples, some at a random spot and
// height), but its draws of the random generator do not step the game's state: whether a surface is in view
// (CWater::CheckClip, against this copy's own camera) differs between two copies with their own cameras, so a copy
// whose player stood by Matataki's water drew three numbers a tick the other did not (w-world-split -Case merge-town, a
// save that has seen the town: both copies equal at the merge's tick, the random state parted the tick after).
// DC_WORLDS_MUTATE=water_rand (twin): its draws step the state, as before.
namespace dc::game { // repin: was PORT_OVERRIDE; a recipe removes the game's own definition
void CEditGround::StepWater() {
    const unsigned int state = PortRandState();

    for (CGroundWater &surface : water_surfaces) {
        if (surface.draw == 0 || surface.water.CheckClip()) {
            continue;
        }

        for (const GROUND_WATER_RIPPLE &ripple : surface.ripples) {
            if (ripple.power == 0.0f && ripple.range == 0.0f) {
                break;
            }

            const int rows = surface.water.rows;
            const int row = ripple.row < 0 ? (int) (rows * (float) rand() / 2.1474836e9f) : (int) ripple.row;
            const int column = ripple.column < 0 ? (int) (rows * (float) rand() / 2.1474836e9f) : (int) ripple.column;

            surface.water.Shake(row, column, ripple.power + ripple.range * (float) rand() / 2.1474836e9f);
        }

        surface.water.Hamon();
    }

    if (!Mutated("water_rand")) {
        PortRandSetState(state);
    }
}
} // namespace dc::game

// The game's own CSaveData::SetGameIntFlag (32 of them), likewise.
namespace dc::game { // repin: was PORT_OVERRIDE; a recipe removes the game's own definition
int CSaveData::SetGameIntFlag(int flag_no, int value) {
    if (flag_no >= 32) {
        return 0;
    }

    const int before = this->game_int_flag[flag_no];

    this->game_int_flag[flag_no] = value;
    FlagChanged(this, 1, flag_no, before, value);
    return value;
}
} // namespace dc::game

// Test hook (DC_WORLDS_TEST_SHOTS=DIR): on the copy that stayed, 5 s and 30 s after each split (wall clock, a copy can
// be slow), its last frame's mean brightness logged ("worlds: test: screen after split N at Ks: mean M") and the frame
// written to DIR\split-N-Ks.png, so a test sees what the stayer draws (Jep's 2026-10-21 black screen after a second split).
static void ShotsAfterSplit() {
    static const char *const dir = TestEnv("DC_WORLDS_TEST_SHOTS");
    static std::int64_t      seen_split = -1;
    static int               split_no = 0;
    static int               taken = 0;
    static auto              since = std::chrono::steady_clock::now();

    if (dir == nullptr || !g_state.apart || !g_state.stayer) {
        return;
    }

    if (g_state.split_tick != seen_split) {
        seen_split = g_state.split_tick;
        split_no++;
        taken = 0;
        since = std::chrono::steady_clock::now();
    }

    static const int at[2] = {5, 30};
    const auto       secs = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - since).count();

    if (taken >= 2 || secs < at[taken]) {
        return;
    }

    std::vector<std::uint8_t> rgba;
    std::uint32_t             width = 0, height = 0;
    double                    sum = 0.0;

    if (dc::app::GameScreenshot(rgba, width, height) && width * height > 0) {
        for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
            sum += (rgba[i] + rgba[i + 1] + rgba[i + 2]) / 3.0;
        }

        char path[512];
        std::snprintf(path, sizeof path, "%s/split-%d-%ds.png", dir, split_no, at[taken]);
        NetplayTestWritePng(path, rgba.data(), width, height);
    }

    std::fprintf(stderr, "worlds: test: screen after split %d at %ds: mean %.1f (%ux%u), town mode %d\n", split_no, at[taken],
                 width * height > 0 ? sum / (width * height) : -1.0, width, height, mode == GAME_MODE_EDIT ? (int) GameMode : -1);
    std::fflush(stderr);
    taken++;
}

// DC_GROUND_TRACE=DIR: in a town, each frame's median brightness of the lower 40% of the screen (the ground in front of
// the camera, whatever a player, the help bar or the compass covers of it) with where Toan stands, the camera and the
// clock, and the two frames of each jump over DC_GROUND_TRACE_JUMP (8 by default) written to DIR as
// p<player>-jump-N-a/b-frameF.png (Jep's "the floor randomly lightens" by Norune's gate at night, 2026-10-24: the fog of
// a ground corner behind the eye). DC_GROUND_TRACE_FROM_CLOCK=C starts it once the town's clock reaches C. Tests only.
static void GroundTrace() {
    static const char *const         dir = TestEnv("DC_GROUND_TRACE");
    static const double              jump = TestEnvInt("DC_GROUND_TRACE_JUMP", 8);
    static const double              from_clock = TestEnvInt("DC_GROUND_TRACE_FROM_CLOCK", 0);
    static std::vector<std::uint8_t> before;
    static int                       was = -1;
    static int                       frame = 0, pairs = 0;

    if (dir == nullptr || mode != GAME_MODE_EDIT || Chara == nullptr || NowTime < from_clock) {
        was = -1;
        return;
    }

    std::vector<std::uint8_t> rgba;
    std::uint32_t             width = 0, height = 0;

    if (!dc::app::GameScreenshot(rgba, width, height) || width < 8 || height < 8) {
        return;
    }

    int histogram[256] = {};
    int count = 0;

    for (std::uint32_t y = height * 60 / 100; y < height * 98 / 100; y += 2) {
        for (std::uint32_t x = 0; x < width; x += 2) {
            const std::uint8_t *p = &rgba[(y * width + x) * 4];
            histogram[(299 * p[0] + 587 * p[1] + 114 * p[2]) / 1000]++;
            count++;
        }
    }

    int level = 0;

    for (int seen = histogram[0]; seen * 2 < count && level < 255; seen += histogram[++level]) {
    }

    sceVu0FVECTOR toan, eye = {0.0f, 0.0f, 0.0f, 0.0f};

    Chara->GetPosition(toan);

    if (NowCamera != nullptr) {
        NowCamera->GetPos(eye);
    }

    frame++;
    // Player 2's copy adds the eye it drew from (her own camera) and that eye's height over her feet.
    char  p2eye[96] = "";
    float drawn[3], over = 0.0f;

    if (CoopTownDrawnEye(drawn, &over)) {
        std::snprintf(p2eye, sizeof p2eye, " p2eye (%.0f %.0f %.0f) over %.0f", drawn[0], drawn[1], drawn[2], over);
    }

    std::fprintf(stderr, "groundlight: frame %d luma %d town mode %d toan (%.0f %.0f) eye (%.0f %.0f %.0f) clock %.3f%s\n", frame, level,
                 (int) GameMode, toan[0], toan[2], eye[0], eye[1], eye[2], NowTime, p2eye);

    if (was >= 0 && std::abs(level - was) > jump && pairs < 6 && before.size() == rgba.size()) {
        char path[512];

        pairs++;
        std::snprintf(path, sizeof path, "%s/p%d-jump-%d-a-frame%d.png", dir, Player(), pairs, frame - 1);
        NetplayTestWritePng(path, before.data(), width, height);
        std::snprintf(path, sizeof path, "%s/p%d-jump-%d-b-frame%d.png", dir, Player(), pairs, frame);
        NetplayTestWritePng(path, rgba.data(), width, height);
        std::fprintf(stderr, "groundlight: jump %d at frame %d: %d to %d\n", pairs, frame, was, level);
    }

    std::fflush(stderr);
    was = level;
    before.swap(rgba);
}

// coop-doorfade test hook (DC_COOP_DOORFADE_SHOT=DIR): while a town door opens on this copy (its fade out), frames 10, 40
// and 70 of it written to DIR\p<player>-door-N-fF.png, so a test (and Jep) can see who is still drawn through the fade.
static void DoorFadeShots() {
    static const char *const dir = TestEnv("DC_COOP_DOORFADE_SHOT");
    static int               frame = 0;
    static int               door = 0;

    if (dir == nullptr) {
        return;
    }

    // (coop-housefade: a room's door too, ED_IN_MODE_DOOR_OPEN)
    // (coop-reds1109, w-indoor-inleave-doorfade red since 52e968ac: a room's door opened together, CoopWorldsDoorWindowStart, its
    // fade EdFadeOut on both copies, is in no door mode: its window counts as the door's opening too)
    if (mode != GAME_MODE_EDIT || (EdInteriorFlag != 0 ? CoopTownIndoorMode() != 1 && CoopWorldsDoorWindowBy() == 0 : GameMode != ED_MODE_DOOR_OPEN)) {
        frame = 0;
        return;
    }

    if (++frame == 1) {
        door++;
    }

    if (frame != 10 && frame != 40 && frame != 70) {
        return;
    }

    std::vector<std::uint8_t> rgba;
    std::uint32_t             width = 0, height = 0;

    if (dc::app::GameScreenshot(rgba, width, height) && width * height > 0) {
        char path[512];

        std::snprintf(path, sizeof path, "%s/p%d-door-%d-f%d.png", dir, Player(), door, frame);
        NetplayTestWritePng(path, rgba.data(), width, height);
        std::fprintf(stderr, "worlds: test: door %d frame %d written (%ux%u)\n", door, frame, width, height);
        std::fflush(stderr);
    }
}

void NetplayTestShotAfterFrame(); // coop-itemsend (netplay.cpp)

void CoopWorldsAfterFrame() {
    NetplayTestShotAfterFrame();
    DoorFadeShots(); // coop-doorfade
    ShotsAfterSplit();
    GroundTrace();
}

static void TestApartPresses(); // coop-saveload (below)
static void QuestStageAtMerge(); // coop-pt1111e (below)
static void TestPickOnPeer();   // coop-startjoin (below)
extern bool g_entry_hide;       // coop-localfix (below)

namespace {

// coop-cutsceneview: the other copy's view of the floor's opening scene it plays (SceneView), as last heard, and when.
struct SceneViewIn {
    bool                                  set = false;
    SceneView                             view{};
    std::chrono::steady_clock::time_point at{};
};
SceneViewIn   g_scene_in;
std::uint32_t g_scene_seq = 0;

// This copy, apart, on its way onto a floor it built (the floor's "go dungeon" not come yet) with a scene of that floor under
// way, the other copy in this dungeon: its view, each tick. The other shows it only while its own floor start is held for
// that scene (CoopWorldsSceneView); it is a few dozen bytes a tick, for a minute or two.
void SendSceneView() {
    int built_map = -1, built_floor = -1;

    if (!g_state.apart || mode != GAME_MODE_DUNGEON || UserStatus == nullptr || BtEventMode == 0 || NowCamera__3 == nullptr ||
        CharaMain.frame == nullptr || (Here().floor != -2 && CoopWorldsSceneJoining() != 1) || !g_state.peer_heard || g_state.peer.mode != GAME_MODE_DUNGEON ||
        g_state.peer.map != selectMapNo || !NetplayFloorBuilt(&built_map, &built_floor) || built_map != selectMapNo ||
        CoopWorldsFloorStartHeld()) { // (this copy waiting on the other's scene plays none of its own)
        return;
    }

    SceneView     view{kMagic, (std::uint32_t) Player(), kSceneView, ++g_scene_seq, built_map, built_floor};
    sceVu0FVECTOR eye;
    sceVu0FVECTOR ref;
    sceVu0FVECTOR place;
    float         axis_x[2];
    float         axis_z[2];

    NowCamera__3->GetPos(eye);
    NowCamera__3->GetRef(ref);
    coop::PlayerDrawnPlace(place, axis_x, axis_z);
    for (int i = 0; i < 3; i++) {
        view.eye[i] = eye[i];
        view.ref[i] = ref[i];
        view.place[i] = place[i];
    }
    view.roll = NowCamera__3->GetRoll();
    view.yaw = atan2f(axis_z[0], axis_z[1]);
    view.chara = UserStatus->cur_chara;
    view.motion = CharaMain.motion_no;
    view.motion_flags = CharaMain.motion_flags;
    // (Player 2's copy draws no player until her member plays: CoopWorldsHideDungeonPlayer.)
    view.drawn = BtActStatus.player_visible != 0 && EdEventInfo.player_draw != 0 && (Player() != 2 || !g_entry_hide) ? 1u : 0u;
    NetSocketSend(&view, sizeof view);
    CoopWorldsScenePicturesSend(kMagic, Player(), view.seq, built_map, built_floor); // coop-tutimages
    // Test hook (DC_COOP_TEST_SCENE_SHOTS=DIR:EVERY, x-floor-join -Case scene): this copy's frame of every EVERY-th view it
    // sends, to DIR/scene-SEQ.png, for the other's frames of the same view (its merge series lines carry the count).
    static const char *const shots = TestEnv("DC_COOP_TEST_SCENE_SHOTS");

    if (shots != nullptr) {
        static std::string dir;
        static long        every = 0;
        static char        path[512];

        if (every == 0) {
            const std::string spec = shots;
            const std::size_t colon = spec.rfind(':');

            dir = spec.substr(0, colon);
            every = std::max(1L, std::strtol(spec.c_str() + colon + 1, nullptr, 10));
        }
        if (view.seq % (std::uint32_t) every == 0) {
            std::snprintf(path, sizeof path, "%s/scene-%05u.png", dir.c_str(), view.seq);
            NetplayTestShotTo(path);
            std::printf("coop-test: scene shot %u pictures %u\n", view.seq, CoopWorldsScenePicturesSent());
        }
    }
    // (Tests: which view this copy sent, so the other's drawn one can be matched to it.)
    if (CoopDebug() && view.seq % 30 == 0) {
        std::fprintf(stderr, "worlds: scene view sent %u: eye (%.1f %.1f %.1f) ref (%.1f %.1f %.1f) player (%.1f %.1f %.1f) drawn %u motion %d\n", view.seq,
                     view.eye[0], view.eye[1], view.eye[2], view.ref[0], view.ref[1], view.ref[2], view.place[0], view.place[1], view.place[2],
                     view.drawn, view.motion);
    }
}

// The other's view fresh (heard in the last half second), of the floor this copy waits to start.
const SceneView *FreshSceneView() {
    const auto age = std::chrono::steady_clock::now() - g_scene_in.at;

    if (!g_scene_in.set || age > std::chrono::milliseconds(500) || UserStatus == nullptr || g_scene_in.view.map != selectMapNo ||
        g_scene_in.view.floor != UserStatus->cur_floor) {
        return nullptr;
    }
    return &g_scene_in.view;
}

// Showing the other's scene, this copy's player (the game's Toan, which the dungeon loaded and which plays no part until the
// floor starts: CoopWorldsHideDungeonPlayer) stands in for the other's where the other draws it, playing what it plays (a
// motion this model lacks: standing). Where it stood, and what it played, come back as the view ends, so a floor that then
// starts alone starts as it would have.
void ScenePoseTick() {
    static struct {
        bool          on = false;
        sceVu0FVECTOR pos;
        sceVu0FVECTOR rotation;
        int           motion = 0;
        int           flags = 0;
    } saved;

    // (Merged: the state this copy took is the pair's; nothing of this copy's own to put back. coop-b1lesson: from the
    // moment it is in, the merge's catch-up included: its player is the other's now.)
    if (!g_state.apart || g_merge_state_in) {
        saved.on = false;
        return;
    }

    const bool show = CoopWorldsSceneShowsOther();

    if (show) {
        const SceneView &view = g_scene_in.view;

        if (!saved.on) {
            saved.on = true;
            sceVu0CopyVector(saved.pos, CharaMain.pos);
            CharaMain.GetRotation(saved.rotation);
            saved.motion = CharaMain.motion_no;
            saved.flags = CharaMain.motion_flags;
            std::fprintf(stderr, "worlds: scene view: this copy's player stands in for player %d's in its scene (at %.0f %.0f %.0f)\n", 3 - Player(),
                         view.place[0], view.place[1], view.place[2]);
            std::fflush(stderr);
        }

        int index = -1;

        CharaMain.SetPosition(view.place[0], view.place[1], view.place[2]);
        CharaMain.SetRotation(0.0f, view.yaw, 0.0f);
        if (view.motion != CharaMain.motion_no) {
            const bool has = CharaMain.GetMotionParam(view.motion, &index, nullptr, nullptr, nullptr) != nullptr && index >= 0;

            CharaMain.SetMotion(has ? view.motion : 0, has ? view.motion_flags : 0);
        }
    } else if (saved.on) {
        saved.on = false;
        CharaMain.SetPosition(saved.pos[0], saved.pos[1], saved.pos[2]);
        CharaMain.SetRotation(saved.rotation[0], saved.rotation[1], saved.rotation[2]);
        CharaMain.SetMotion(saved.motion, saved.flags);
        std::fprintf(stderr, "worlds: scene view: this copy's player back where it stood\n");
        std::fflush(stderr);
    }
}

} // namespace

bool CoopWorldsSceneView(float *eye, float *ref, float *roll) {
    const SceneView *view = On() && g_state.apart && CoopWorldsFloorStartHeld() && !Mutated("scene_black") ? FreshSceneView() : nullptr;
    static bool      said = false;

    if ((view != nullptr) != said) {
        said = view != nullptr;
        std::fprintf(stderr, "worlds: scene view %s: player %d's opening scene of floor %d %s\n", said ? "on" : "off", 3 - Player(),
                     UserStatus != nullptr ? (int) UserStatus->cur_floor + 1 : 0, said ? "shown on this copy, from its camera" : "no longer shown");
        std::fflush(stderr);
    }
    if (view == nullptr) {
        return false;
    }
    for (int i = 0; i < 3; i++) {
        if (eye != nullptr) {
            eye[i] = view->eye[i];
        }
        if (ref != nullptr) {
            ref[i] = view->ref[i];
        }
    }
    if (eye != nullptr) {
        eye[3] = 1.0f;
    }
    if (ref != nullptr) {
        ref[3] = 1.0f;
    }
    if (roll != nullptr) {
        *roll = view->roll;
    }
    // (Tests: the view this copy draws from, each 30th the other sent, as SendSceneView logs those.)
    static std::uint32_t logged = 0;

    if (CoopDebug() && eye != nullptr && view->seq % 30 == 0 && view->seq != logged) {
        logged = view->seq;
        std::fprintf(stderr, "worlds: scene view drawn %u: eye (%.1f %.1f %.1f) ref (%.1f %.1f %.1f) player (%.1f %.1f %.1f) shown %d motion %d\n",
                     view->seq, view->eye[0], view->eye[1], view->eye[2], view->ref[0], view->ref[1], view->ref[2], CharaMain.pos[0],
                     CharaMain.pos[1], CharaMain.pos[2], CoopWorldsSceneShowsOther() ? 1 : 0, (int) CharaMain.motion_no);
    }
    return true;
}

unsigned CoopWorldsSceneViewSeq() {
    return CoopWorldsSceneView(nullptr, nullptr, nullptr) ? g_scene_in.view.seq : 0u;
}

bool CoopWorldsSceneShowsOther() {
    return CoopWorldsSceneView(nullptr, nullptr, nullptr) && g_scene_in.view.drawn != 0 && UserStatus != nullptr &&
           g_scene_in.view.chara == UserStatus->cur_chara && CharaMain.frame != nullptr && CharaMain.frame->reference == 0;
}

void CoopWorldsArrivalSeenOutTick(); // coop-soakfix3 (below, beside her arrival run as seen)

void CoopWorldsTick(bool up, std::int64_t tick) {
    CoopStorageTick(); // coop-small2: the storage asks apart, resent till answered
    CoopWorldsArrivalSeenOutTick();
    // coop-localfix: out of a dungeon, the next one's first frames hide player 2's Toan (CoopWorldsHideDungeonPlayer).
    if (mode != GAME_MODE_DUNGEON) {
        g_entry_hide = true;

        // coop-startjoin: test hook (DC_WORLDS_TEST_STALE_START=1, x-floor-join -StartJoin): player 2's copy, apart in town,
        // once: the cave's floor 1 as the last floor started, as her trip there before Jep's crash left it (her escape).
        static bool stale_set = false;

        if (!stale_set && Player() == 2 && g_state.apart && TestEnvInt("DC_WORLDS_TEST_STALE_START", 0) != 0) {
            stale_set = true;
            g_floor_started_key = 0;
            std::fprintf(stderr, "worlds: test: the cave's floor 1 is the last floor started on this copy\n");
        } else if (!Mutated("stale_floor_start")) {
            g_floor_started_key = -1000; // coop-startjoin: out of the dungeon, its last floor's start is spent (Here())
        }
    }

    // Once, for a linked copy: whether separate worlds are on.
    if (!g_state.announced && Player() != 0) {
        g_state.announced = true;
        std::fprintf(stderr, On() ? "worlds: separate worlds on (player %d of a linked pair)\n"
                                  : "worlds: separate worlds off (player %d of a linked pair; DC_COOP_WORLDS=0)\n",
                     Player());
        std::fflush(stderr);
    }

    if (!On()) {
        return;
    }
    CallTick(); // coop-floorgate
    ScenePoseTick(); // coop-cutsceneview
    if (mode == GAME_MODE_EDIT) {
        doorgate::Scan(); // coop-doorgate: the door table, once (at the first town tick)
    }

    // coop-scenejoin (tests, DC_WORLDS_TEST_BAGDUMP): both bags and the dumped flags as a room's story scene ends together.
    static bool scene_was = false;
    const bool  scene_now = !g_state.apart && SceneIndoors() && EdInSceneEvent() > 0;

    if (scene_was && !scene_now) {
        slotsync::BagDump("scene over");
    }
    scene_was = scene_now;

    // coop-exitscene: a town's event outdoors together (after a merge into it, or begun together): its start and end on this
    // copy, with the link ticks (stderr; the tests read it).
    static bool town_scene_was = false;
    const bool  town_scene_now = g_state.up && !g_state.apart && SceneOutdoors();

    if (town_scene_now != town_scene_was) {
        std::fprintf(stderr, "worlds: %s at link tick %lld (player %d's copy)\n",
                     town_scene_now ? "a town event runs together, both screens on its camera" : "the town event together is over", (long long) g_state.tick, Player());
        std::fflush(stderr);
    }
    town_scene_was = town_scene_now;
    // (coop-b1lesson: the other's state in, its texture blocks are the other's: this copy frees its buffers only.)
    CoopWorldsScenePicturesTick(g_state.apart && !g_merge_state_in, selectMapNo, UserStatus != nullptr ? (int) UserStatus->cur_floor : -1); // coop-tutimages
    // coop-b1lesson: the scene's packs, said (a scene under way) and read (joining the other's, until it is over).
    {
        static bool joined = false;
        const bool  scene = mode == GAME_MODE_DUNGEON && BtEventMode != 0;

        joined = (joined && scene && mode == GAME_MODE_DUNGEON) || CoopWorldsSceneJoining() == 2;
        CoopWorldsScenePacksTick(scene, joined);
    }
    coop::EntryStandInTick(); // coop-joinvisuals

    // coop-worlds: live sync while apart (the shared slots both ways; the test's slot change first).
    // coop-mergebags: not while a catch-up merge replays the host's play from its checkpoint: the replay rewinds the slots
    // to the checkpoint's (the mayor's key gone from player 1's bag), and each rewound slot went to the host as this copy's
    // own change, emptying his bag there (Jep's 2026-10-25: "I never made it out of the house as p1 with the key").
    if (g_state.apart && (Mutated("scan_in_catchup") || !NetplayCatchUnderWay())) {
        slotsync::TestTick();
        slotsync::Tick();
    }
    TestApartPresses();
    TestPickOnPeer();
    if ((g_state.apart || !NetplayLinkUp()) && Player() == 2) { // (coop-georamaload: her copy waiting to join too)
        CoopTownTestApartUnbuild(); // coop-uiflash: DC_COOP_TEST_UNBUILD_APART
    }

    // coop-apartsync test hooks (w-apartsync), K ticks of play apart (or alone since the link went down): DC_WORLDS_TEST_PARTY=K:N,
    // this copy's party becomes N as a town script's _SSET_PARTY_NUM (710, a character joining) makes it;
    // DC_WORLDS_TEST_SCRIPT_ITEM=K:ITEM, this copy's player gets ITEM as a town script's _SITEM_GET (702) gives it (the save's
    // party pack: her bag on her copy apart). coop-fx51c (Jep, 2026-11-04: no fixed tick waits): K "heard" (heard:N), once this
    // copy apart has heard where the other is (its presence: the record then goes to a copy that hears it).
    static const char *const test_party = TestEnv("DC_WORLDS_TEST_PARTY");
    static const char *const test_item = TestEnv("DC_WORLDS_TEST_SCRIPT_ITEM");
    static bool              test_party_done = false, test_item_done = false, test_ever_linked = false;

    test_ever_linked = test_ever_linked || NetplayPartnerLinked();
    if ((test_party != nullptr || test_item != nullptr) && SaveData != nullptr && mode == GAME_MODE_EDIT &&
        (g_state.apart || (test_ever_linked && !NetplayLinkUp()))) {
        long long k = -1;
        int       n = 0;
        // K:N, or heard:N (due once the other copy is heard apart)
        const auto due = [&](const char *spec) {
            if (std::strncmp(spec, "heard:", 6) == 0) {
                n = std::atoi(spec + 6);
                return g_state.apart && g_state.peer_heard;
            }
            return std::sscanf(spec, "%lld:%d", &k, &n) == 2 && g_state.apart_hook_ticks >= k;
        };

        g_state.apart_hook_ticks++;
        if (!test_party_done && test_party != nullptr && due(test_party)) {
            test_party_done = true;
            std::fprintf(stderr, "worlds: test: this copy (apart, player %d) makes the party %d, as _SSET_PARTY_NUM (clock %lld ms)\n", Player(), n, ClockMs());
            SaveData->GetDngStatus()->SetPartySize((s8) n);
        }
        if (!test_item_done && test_item != nullptr && due(test_item)) {
            test_item_done = true;
            std::fprintf(stderr, "worlds: test: this copy (apart, player %d) gets item %d, as _SITEM_GET (clock %lld ms)\n", Player(), n, ClockMs());
            EdGetItem(n, 1, GetAddAttachItem(n) != 0 ? 3 : -1);
            slotsync::BagDump("script item");
        }
        std::fflush(stderr);
    }

    // The leaver out of the town it left (another place), or walking there again for 2 s (its way out undone).
    if (g_state.leaving && !(Here() == g_state.left_from)) {
        g_state.leaving = false;
        Log(g_state.left_from.mode == GAME_MODE_DUNGEON ? "left the floor: this copy may merge again" : "left the town: this copy may merge again");
    } else if (g_state.leaving && mode == GAME_MODE_DUNGEON) {
        // Off a floor: undone once it plays on the floor again for 5 s (no fade, no Escape under way).
        const bool plays = gameTask == GAME_TASK_PLAY && BtGameModeFlag == BT_GAME_MODE_PLAY;

        g_state.leaving_walk = plays ? g_state.leaving_walk + 1 : 0;
        if (g_state.leaving_walk >= 300) {
            g_state.leaving = false;
            Log("the way out did not happen: this copy plays on the floor again and may merge");
        }
    } else if (g_state.leaving && mode == GAME_MODE_EDIT && GameMode == ED_MODE_WALK && ++g_state.leaving_walk >= 120) {
        g_state.leaving = false;
        Log("the way out did not happen: this copy walks in the town again and may merge");
    } else if (g_state.leaving && GameMode != ED_MODE_WALK) {
        g_state.leaving_walk = 0;
    }

    // DC_COOP_DEBUG: where this copy is while together, at each change (the tests read it after a merge).
    if (CoopDebug() && up && !g_state.apart) {
        static Zone logged;
        const Zone  here = Here();

        if (!(here == logged)) {
            char where[96];

            logged = here;
            Describe(here, where, sizeof where);
            std::fprintf(stderr, "worlds: together: this copy (player %d) is %s\n", Player(), where);
            std::fflush(stderr);
        }
    }

    // DC_COOP_DEBUG, coop-uiflash: her facing 30 link ticks after a town merge (a tick both copies share), so a test can
    // compare it on both copies and with hers before the merge (Jep's 2026-10-26: she "faces towards the house").
    if (CoopDebug() && g_merge_face_at >= 0 && tick >= g_merge_face_at) {
        float partner[4];
        float rot[4];

        g_merge_face_at = -1;
        if (mode == GAME_MODE_EDIT && CoopTownPartnerPose(partner, rot)) {
            std::fprintf(stderr, "worlds: her facing 30 ticks after the merge (link tick %lld): %.2f at (%.0f %.0f)\n", (long long) tick, rot[1],
                         partner[0], partner[2]);
            std::fflush(stderr);
        }
    }

    // DC_COOP_DEBUG, coop-mergestand: once a second in a town outdoors, where this copy has both players (Toan's body,
    // which apart on player 2's copy is hers; the town partner, if shown), so a test can compare the positions before
    // and after a merge on both copies (Jep's 2026-10-25 notes 4 and 5: the merge put both at the arrival).
    if (CoopDebug() && mode == GAME_MODE_EDIT && EdInteriorFlag == 0 && Chara != nullptr && Chara->frame != nullptr) {
        static int second = 0;

        // coop-houseexit: linked, on the link's own ticks (each 60th), so both copies log the same ticks and a test compares
        // like with like (each copy's own count logged ticks apart: l-local-play's end-place check saw her walk on one only).
        if (up && !g_state.apart ? tick % 60 == 0 : ++second >= 60) {
            float at[4];
            float partner[4];
            float rot[4];

            second = 0;
            Chara->GetPosition(at);

            // coop-small1b (~saidwait's flake): unlinked only, as the identity's latch is. `up` is also false while a catch-up
            // runs (CoopWorldsTick's caller: linked, not yet in lockstep), so a replay that spanned 60 of these frames said
            // "no link" while linked and set the latch by chance: then the -HerFirst hook, re-armed at his way out by its twin
            // (said_wait_every_link), found her spot said and held nothing, and the twin passed (1 run in 3 on 11-09).
            if (!g_state.apart && !up && !NetplayLinkUp()) {
                g_prelink_spot = true; // coop-localrace (CoopWorldsPrelinkSaid)
            }
            if (CoopTownPartnerPose(partner, rot)) {
                std::fprintf(stderr, "worlds: town positions (%s, link tick %lld): Toan at (%.0f %.0f %.0f), partner at (%.0f %.0f %.0f)\n",
                             g_state.apart ? "apart" : up ? "together" : "no link", (long long) tick, at[0], at[1], at[2], partner[0], partner[1], partner[2]);
            } else {
                std::fprintf(stderr, "worlds: town positions (%s, link tick %lld): Toan at (%.0f %.0f %.0f), no partner\n",
                             g_state.apart ? "apart" : up ? "together" : "no link", (long long) tick, at[0], at[1], at[2]);
            }

            std::fflush(stderr);
        }
    }

    // DC_COOP_DEBUG, coop-joindesync: linked in a town outdoors, both players' places on every 60th link tick (a tick both
    // copies share, unlike the line above's own count), so a test pairs the copies' lines tick by tick after a join.
    if (CoopDebug() && up && !g_state.apart && mode == GAME_MODE_EDIT && EdInteriorFlag == 0 && Chara != nullptr && Chara->frame != nullptr &&
        tick > 0 && tick % 60 == 0) {
        float at[4];
        float partner[4];
        float rot[4];
        bool  shown = CoopTownPartnerPose(partner, rot);

        Chara->GetPosition(at);
        std::fprintf(stderr, "worlds: town pair at link tick %lld: Toan (%.2f %.2f %.2f), partner %s(%.2f %.2f %.2f)\n", (long long) tick, at[0], at[1],
                     at[2], shown ? "" : "hidden ", shown ? partner[0] : 0.0f, shown ? partner[1] : 0.0f, shown ? partner[2] : 0.0f);
        std::fflush(stderr);
    }

    // Test hook (DC_WORLDS_FADE_LOG=1): on a floor, each time the screen's fades (the dungeon's DispFade and the
    // editor fade an Escape uses, 0 clear to 128 black) darken it past 32, and each time it clears again (16 or
    // less), with the darkest of the spell. Read only.
    static const bool fade_log = TestEnv("DC_WORLDS_FADE_LOG") != nullptr;

    if (fade_log && mode == GAME_MODE_DUNGEON) {
        static bool  dark = false;
        static float darkest = 0.0f;
        float        colour[4];

        EdGetFadeColor(colour);

        float disp = DispFade__3.GetRate();
        float fade = std::max(disp, colour[3]);

        if (!dark && fade > 32.0f) {
            dark = true;
            darkest = fade;
            std::fprintf(stderr, "worlds: test: the screen goes dark (fade %.0f: dungeon %.0f, editor %.0f), %s, task 0x%x\n", fade, disp, colour[3],
                         g_state.apart ? "apart" : "together", (unsigned) gameTask);
            std::fflush(stderr);
        } else if (dark) {
            darkest = std::max(darkest, fade);

            if (fade <= 16.0f) {
                dark = false;
                std::fprintf(stderr, "worlds: test: the screen clears (darkest %.0f)\n", darkest);
                std::fflush(stderr);
            }
        }
    }

    const bool was_up = g_state.up;
    const bool was_linked = g_state.was_linked;

    g_state.up = up;
    g_state.was_linked = NetplayPartnerLinked();
    FollowApart(); // coop-dungeonp2: a link coming up while apart (a merge starting) puts the bags home first
    slotsync::RingSweep(); // coop-keyring

    if (mode == GAME_MODE_DUNGEON) {
        coop::KeyRingTest(true); // coop-keyring: its test lines (DC_COOP_KEYRING)
    }

    // coop-apartsync: a drop of the lockstep (not a split, not the other player's Leave, not a merge that did not hold) in a
    // town or dungeon: apart in place (DropApart).
    // The link itself down (NetplayLinkUp): a resync under way (a load's, an OUT OF SYNC's) holds the ticks with the link up.
    if (was_up && was_linked && !up && !NetplayLinkUp() && !g_state.apart && !g_state.merge_pending && Player() != 0 && !NetplayPeerQuit() &&
        (mode == GAME_MODE_EDIT || mode == GAME_MODE_DUNGEON) && !Mutated("no_drop_apart")) {
        DropApart();
    }

    // coop-mergebags: the bag test's dump every 600 of this copy's ticks (10 s), together or apart.
    static std::int64_t dump_ticks = 0;

    if (dump_ticks++ % 600 == 0) {
        slotsync::BagDump("every 10 s");
    }
    g_state.tick = tick;

    auto now = std::chrono::steady_clock::now();

    // A merge that has held a while is done; one whose link went down at once did not take (one copy linked
    // and the other did not): the copies are apart again and look for each other as before.
    if (g_state.merge_pending && !up && !Mutated("no_rejoin_apart")) {
        g_state.merge_pending = false;
        g_state.apart = true;
        FollowApart(); // coop-dungeonp2
        g_state.peer_ready = false; // what each heard of the other still holds; only its readiness is stale
        g_state.stayer = g_state.merged_stayer; // still the one that stayed (or left) before the merge that did not take
        g_state.first_here = g_state.merged_first_here;
        Log("the merge did not hold (the link went down at once); apart again");

        if (g_state.floor_hold) {
            g_state.floor_hold_since = now;
            Log("the floor change stays held for the merge");
        }
    } else if (g_state.merge_pending && now - g_state.merged_at > std::chrono::milliseconds(kMergeHoldMs)) {
        MergeHeld();
    }

    // coop-pt1111j: DC_COOP_DEBUG, 30 of this copy's frames after each split (the next merge has not begun): who this copy
    // counts as at the split now, what the next merge's direction and stand read (x-door-again).
    if (CoopDebug() && g_state.apart) {
        static auto split_seen = std::chrono::steady_clock::time_point{};
        static int  frames = 0;

        if (g_state.split_at != split_seen) {
            split_seen = g_state.split_at;
            frames = 0;
        }
        if (++frames == 30) {
            char said[200];

            std::snprintf(said, sizeof said, "30 frames after the split at tick %lld: this copy (player %d) is the one that %s, there first %d", (long long) g_state.split_tick,
                          Player(), g_state.stayer ? "stayed" : "left", g_state.first_here ? 1 : 0);
            Log(said);
        }
    }


    // A link that came up while apart is the merge (CoopWorldsMayLink let it start): together again.
    if (g_state.apart && up) {
        char where[96];
        char line[200];

        // Test hook (DC_WORLDS_TEST_REFUSE_MERGES=N): this copy drops the first N merges' links at once, as a link that
        // came up on one copy only does under a bad network (the harsh lag run, 2026-10-21): the other's merge does not hold.
        static const int refuse = TestEnvInt("DC_WORLDS_TEST_REFUSE_MERGES", 0);
        static int       refused = 0;

        if (refused < refuse) {
            refused++;
            std::snprintf(line, sizeof line, "test: refusing merge %d of %d (the link is dropped at once)", refused, refuse);
            Log(line);
            g_state.peer_ready = false;
            NetplayGoApart("test: a merge refused");
            return;
        }

        // A test's floor hooks, run alone while apart, stop: they press pad 1 on this copy only.
        NetplayTestFloorMerged();

        PickJoinMerged(); // coop-cutscenejoin: a join from this copy's own arrival: its floor counts as started from here
        Describe(Here(), where, sizeof where);
        g_state.merges++;
        g_state.merge_tick = tick; // coop-heldscene
        // coop-pt1109a: and who started a door or exit is the pair's from here, whatever a copy kept from before (a link that dropped
        // with her event just started, DropApart).
        if (!Mutated("starter_kept_apart")) {
            g_town_starter = 1;
        }
        std::snprintf(line, sizeof line, "merged at link tick %lld %s: together again (this copy player %d, the %s)", (long long) tick, where,
                      Player(), g_state.stayer ? "one that stayed" : "one that left");
        Log(line);
        QuestStageAtMerge(); // coop-pt1111e (test hook)
        slotsync::BagDump("merged");
        g_merge_face_at = tick + 30;
        // Merged inside a building: the merge's load puts both in the town outside, so both go back in on their
        // first walk (CoopWorldsReenterBuilding). DC_WORLDS_MUTATE=no_reenter (twin): they stay outside.
        const Zone merged_at = Here();

        // coop-housemerge: a catch-up merge loads nothing (the host plays on inside, the joiner came in by his door), so
        // both are inside already: going back in on their first walk out sent them in again (OUT OF SYNC, 2026-10-25).
        // DC_WORLDS_MUTATE=reenter_after_catchup (twin): as before.
        const bool loads = !NetplayLinkedByCatchup() || Mutated("reenter_after_catchup");

        g_reenter_building = merged_at.mode == GAME_MODE_EDIT && merged_at.floor > 0 && loads && !Mutated("no_reenter") ? merged_at.floor : 0;

        // coop-housemerge: a merge inside that holds without a load (a catch-up: the host plays on inside) has her placed in
        // the room on both copies; a merge whose load follows places her again anyway.
        // coop-houseenter (Jep, 2026-10-27: "the camera jump was when P1 entered the house"): not after a merge by her state
        // (she was in the room first): she stands where she stood and her camera goes on as it was, on both copies (his took
        // her place and her camera with her state set). Placing her again put her camera back on the room's, his Toan's.
        if (merged_at.mode == GAME_MODE_EDIT && merged_at.floor > 0 && NetplayStayerMerge() && !Mutated("stayer_outdoors")) {
            Log("merged inside by her state: she stays where she stood");
            // coop-heldscene: a room's story arrival her copy held (coop-p2scene) is his: it plays on the first walk step
            // (CoopWorldsArrivalAgain).
            g_state.arrival_again = !Mutated("held_scene_lost");
            g_state.arrival_again_zone = merged_at;
        } else if (merged_at.mode == GAME_MODE_EDIT && merged_at.floor > 0) {
            coop::TownMergedIndoors();
        } else if (merged_at.mode == GAME_MODE_EDIT && NetplayLinkedBySnapshot()) {
            coop::TownMergedOutdoors();
        }
        g_state.apart = false;
        g_state.dropped = false; // coop-apartsync
        g_merge_state_in = false; // coop-localfix
        FollowApart(); // coop-dungeonp2
        g_state.merged_stayer = g_state.stayer;
        g_state.stayer = false;
        g_state.merged_first_here = g_state.first_here;
        g_state.first_here = false;
        g_state.status[0] = '\0';
        g_state.merge_pending = true;
        // A gate held for this merge stays held until the merge's reload takes this copy off the floor: a merge that does
        // not hold (the other copy refused the link) must not let this copy's gate go on alone (harsh lag run, 2026-10-21).
        // DC_WORLDS_MUTATE=release_hold lets it go at the merge, as before (w-world-split -Case next-floor -RefuseMerges 1 twin).
        if (Mutated("release_hold")) {
            g_state.floor_hold = false;
        }
        g_state.merged_at = now;
        return;
    }

    if (!g_state.apart) {
        // Presence goes on until a merge has held: the other copy may still wait for word that this one is
        // here, and links only once it has it.
        if (g_state.merge_pending && now >= g_state.presence_due) {
            SendPresence();
            g_state.presence_due = now + std::chrono::milliseconds(500);
        }

        return;
    }

    if (CoopDebug() && mode == GAME_MODE_EDIT) {
        static int logged_mode = -100;

        if (GameMode != logged_mode) {
            logged_mode = GameMode;
            std::fprintf(stderr, "worlds: apart in town, town mode %d\n", GameMode);
            std::fflush(stderr);
        }
    }

    // Deeper floors than this copy last told of, then whatever the other has not acknowledged. coop-flagsync: both ways (a
    // floor player 1 reached first is no first visit on her copy: Jep's 2026-10-25 note 36, "P2 joined B1 after Toan did and
    // had to watch the cutscene again"); DC_WORLDS_MUTATE=no_floor_back (twin): player 2's alone, as before.
    // In town the save's dungeon status (UserStatus is the dungeon's own).
    CUserStatus *floors = UserStatus != nullptr ? UserStatus : SaveData != nullptr ? (CUserStatus *) SaveData->GetDngStatus() : nullptr;

    // coop-joinatla (tests): what the floor menu counts, once each time it opens apart (DEnterMenu, as InitDunEnterMenu set it).
    static bool menu_told = false;

    if (mode == GAME_MODE_DUNGEON && BtGameModeFlag == BT_GAME_MODE_ENTRANCE_MENU && DEnterMenu.textures_ready != 0) {
        if (!menu_told && CoopDebug() && floors != nullptr) {
            int                 floor1 = 0;
            const std::uint32_t print = DEnterMenu.dungeon >= 0 && DEnterMenu.dungeon < 6 ? AtlaPrint(floors, DEnterMenu.dungeon, &floor1) : 0;

            std::fprintf(stderr, "worlds: test: floor menu of dungeon %d: floor 1 Atla %d/%d (atla print %08x)\n", (int) DEnterMenu.dungeon,
                         (int) DEnterMenu.collected_atra[0], (int) DEnterMenu.max_atra[0], print);
            std::fflush(stderr);
        }
        menu_told = true;
    } else {
        menu_told = false;
    }

    if (g_state.floors_set && floors != nullptr) {
        for (int dungeon = 0; dungeon < 7; dungeon++) {
            if (floors->floor_reached[dungeon] > g_state.floors_told[dungeon]) {
                g_state.floors_told[dungeon] = floors->floor_reached[dungeon];
                std::fprintf(stderr, "worlds: test: this copy (apart, player %d) reaches floor %d in dungeon %d (clock %lld ms)\n", Player(),
                             (int) floors->floor_reached[dungeon], dungeon, ClockMs());
                std::fflush(stderr);
                if (Player() == 2 || !Mutated("no_floor_back")) {
                    // coop-joinatla: b the random state this copy laid the dungeon's Atla out from, d 1 when it has one.
                    const bool laid = dungeon < 6 && g_atla_rand_set[dungeon];

                    QueueProgress(kWhatFloor, floors->floor_reached[dungeon], laid ? (std::int32_t) g_atla_rand[dungeon] : 0, dungeon,
                                  laid ? 1 : -1);
                }
            }
        }
    }

    // coop-apartsync: a character who joined the party on this copy apart (a town script's _SSET_PARTY_NUM: Xiao, Goro ...)
    // joins on the other copy too, so either player can switch to them before the two meet again (only story flags went
    // before, and the other copy's party stayed as it was). DC_WORLDS_MUTATE=no_party_sync (twin): not told.
    if (g_state.floors_set && floors != nullptr && floors->party_size > g_state.party_told) {
        g_state.party_told = floors->party_size;
        if (!Mutated("no_party_sync")) {
            QueueProgress(kWhatParty, floors->party_size, 0, 0, 0);
        }
    }

    statsync::Tick(); // coop-apartstats: her Fruit of Eden, Gourd, kills ... go to the other copy too

    // DC_WORLDS_TEST_SET_FLAG=K:bit|int|map:F:V (the test's): an apart copy sets game flag F (int flag F, or its map's flag
    // F) to V after K ticks apart, through the setter the town scripts' _SSET_GAME_FLAG, _SSET_GAME_INT_FLAG and
    // _SSET_MAP_FLAG call. (coop-b2join: F may be a list, F1/F2/..., each set alike.)
    static const char *const set_flag = TestEnv("DC_WORLDS_TEST_SET_FLAG");
    static bool              set_flag_done = false;

    if (set_flag != nullptr && !set_flag_done && SaveData != nullptr && ++g_state.flag_ticks >= std::atoi(set_flag)) {
        char kind[8] = "";
        char list[200] = "";
        int  after = 0, value = 1;

        set_flag_done = true;
        if (std::sscanf(set_flag, "%d:%7[a-z]:%199[0-9/]:%d", &after, kind, list, &value) == 4 && list[0] != 0) {
            const int kind_no = std::strcmp(kind, "int") == 0 ? kFlagInt : std::strcmp(kind, "map") == 0 ? kFlagMap : kFlagGame;

            for (const char *at = list; *at != 0; at = std::strchr(at, '/') != nullptr ? std::strchr(at, '/') + 1 : at + std::strlen(at)) {
                const int flag = std::atoi(at);
                char      name[48];

                // map: this copy's map (its town, or the building it is in), as _SSET_MAP_FLAG does.
                std::fprintf(stderr, "worlds: test: this copy (apart, player %d) sets %s to %d (clock %lld ms)\n", Player(),
                             FlagName(kind_no, flag, MapNo, name, sizeof name), value, ClockMs());
                std::fflush(stderr);
                if (kind_no == kFlagInt) {
                    SaveData->SetGameIntFlag(flag, value);
                } else if (kind_no == kFlagMap) {
                    SaveData->SetMapFlag(MapNo, flag, value);
                } else {
                    SaveData->SetGameFlag(flag, value);
                }
            }
        } else {
            std::fprintf(stderr, "worlds: test: DC_WORLDS_TEST_SET_FLAG '%s' not understood\n", set_flag);
            std::fflush(stderr);
        }
    }

    SendProgress(now);

    // Twice a second, and at once when this copy is somewhere new; player 2 ready to merge in a town each tick, so the host
    // keeps her where she last stood (coop-spawn).
    if (now >= g_state.presence_due || !(Here() == g_state.sent_here) || MergeReady() != g_state.sent_ready || (BusyHere() ? 1u : SceneHere() ? 2u : 0u) != g_state.sent_busy ||
        (Player() == 2 && g_state.sent_ready && Here().mode == GAME_MODE_EDIT)) {
        SendPresence();
        g_state.presence_due = now + std::chrono::milliseconds(500);
    }
    SendSceneView(); // coop-cutsceneview

    Zone here = Here();

    // coop-houseenter: this copy changed zone: it arrives wherever it goes now, the other was there first (if it is there).
    if (!(here == g_state.first_here_zone)) {
        g_state.first_here_zone = here;
        if (g_state.first_here) {
            g_state.first_here = false;
            Log("there first: no longer (this copy changed zone)");
        }
    }

    if (!(here == g_state.logged_here) || !(g_state.peer == g_state.logged_peer) || g_state.peer_heard != g_state.logged_heard) {
        char mine[96];
        char theirs[96];
        char line[240];

        Describe(here, mine, sizeof mine);
        Describe(g_state.peer, theirs, sizeof theirs);
        std::snprintf(line, sizeof line, "apart: this copy (player %d) is %s (mode %d map %d floor %d); player %d is %s (mode %d map %d floor %d)%s",
                      Player(), mine, here.mode, here.map, here.floor, 3 - Player(), theirs, g_state.peer.mode, g_state.peer.map,
                      g_state.peer.floor, g_state.peer_heard ? "" : " (not heard yet)");
        Log(line);
        g_state.logged_here = here;
        g_state.logged_peer = g_state.peer;
        g_state.logged_heard = g_state.peer_heard;
    }

    UpdateStatus();
}

bool CoopWorldsReceive(std::uint32_t magic, const void *data, int size) {
    if (magic != kMagic) {
        return false;
    }

    if (!On()) {
        return true;
    }

    // coop-small2: the party storage's ask (the host) or answer (the asker), by its size and tag.
    if (CoopStorageReceive(data, size)) {
        return true;
    }

    // A live-sync record or its acknowledgement (by its size).
    if (slotsync::Receive(data, size)) {
        return true;
    }

    // coop-tutimages: the other's scene pictures (coop_scenepics.cpp; by size and kind).
    if (CoopWorldsScenePicturesReceive(data, size, 3 - Player())) {
        return true;
    }

    // coop-apartstats: the other's characters now.
    if (statsync::Receive(data, size)) {
        return true;
    }

    if (size == (int) sizeof(Progress)) {
        Progress progress;

        std::memcpy(&progress, data, sizeof progress);

        if (progress.kind == kProgress) {
            TakeProgress(progress);
        } else if (progress.kind == kProgressAck && progress.player == (std::uint32_t) (3 - Player())) {
            for (auto it = g_state.progress_out.begin(); it != g_state.progress_out.end(); ++it) {
                if (it->id == progress.id) {
                    std::fprintf(stderr, "worlds: progress %u acknowledged by player %u\n", progress.id, progress.player);
                    std::fflush(stderr);
                    g_state.progress_out.erase(it);
                    break;
                }
            }
        }

        return true;
    }

    // coop-cutsceneview: the other's view of its floor's opening scene.
    if (size == (int) sizeof(SceneView)) {
        SceneView view;

        std::memcpy(&view, data, sizeof view);
        if (view.kind == kSceneView && view.player == (std::uint32_t) (3 - Player())) {
            g_scene_in.set = true;
            g_scene_in.view = view;
            g_scene_in.at = std::chrono::steady_clock::now();
        }
        return true;
    }

    if (size != (int) sizeof(Message)) {
        return true;
    }

    Message message;

    std::memcpy(&message, data, sizeof message);

    // Test hook (DC_WORLDS_TEST_DEAF_READY=N): this copy does not hear the other's first N presences that say it is ready
    // to merge, as a copy whose word of it came late or not at all does: the other has started the merge's link by then and
    // says no more (w-world-split -Case next-floor -DeafReady 3; twin DC_WORLDS_MUTATE=no_peer_linking on this copy).
    static const int deaf = TestEnvInt("DC_WORLDS_TEST_DEAF_READY", 0);
    static int       deafened = 0;

    if (message.kind == kPresence && message.ready != 0 && deafened < deaf && g_state.apart) {
        deafened++;
        std::fprintf(stderr, "worlds: test: not hearing player %u's ready presence %d of %d\n", message.player, deafened, deaf);
        std::fflush(stderr);
        return true;
    }

    if (message.kind == kPresence && (g_state.apart || g_state.merge_pending) && message.seq > g_state.peer_seq) {
        g_state.peer_seq = message.seq;
        // coop-houseenter: the other copy changed zone: it arrives, this copy (where it is now) was there first.
        if (const Zone heard{message.mode, message.map, message.floor}; g_state.apart && !(heard == g_state.peer) && !g_state.first_here) {
            g_state.first_here = true;
            g_state.first_here_zone = Here();
            Log("there first: yes (the other copy changed zone)");
        }
        g_state.peer = Zone{message.mode, message.map, message.floor};
        // (coop-gatevote: the pick hook's latch; a presence read in the same tick as the next one is otherwise never seen)
        if (message.mode == GAME_MODE_DUNGEON && (message.floor == -2 || message.floor == -1)) {
            g_test_peer_entrance.map = message.map;
            (message.floor == -2 ? g_test_peer_entrance.splash : g_test_peer_entrance.menu) = true;
        }
        g_state.peer_sees = Zone{message.heard_mode, message.heard_map, message.heard_floor};
        g_state.peer_sees_set = message.heard != 0;
        g_state.peer_heard = true;
        // coop-areacard: the other turned ready while this copy is: a Hello at once, so a host ready first links on it now
        // (her copy links only on the host's link; the host only in a frame with her Hello).
        if (message.ready != 0 && !g_state.peer_ready && g_state.apart && MergeReady()) {
            NetplayHelloNow();
        }
        g_state.peer_ready = message.ready != 0;
        g_state.peer_at = std::chrono::steady_clock::now();
        g_state.peer_stand.set = message.has_pos == 1;
        g_state.peer_floor_stand.set = message.has_pos == 2; // coop-dungeonjoin
        g_state.peer_floor_stand.zone = g_state.peer;
        g_state.peer_floor_stand.pos[0] = message.pos[0];
        g_state.peer_floor_stand.pos[1] = message.pos[1];
        g_state.peer_floor_stand.pos[2] = message.pos[2];
        g_state.peer_floor_stand.yaw = message.yaw;
        g_state.peer_floor_stand.chara = message.chara;
        g_state.peer_stand.map = message.floor > 0 ? (message.map | coop::kIndoorStand) : message.map;
        g_state.peer_stand.pos[0] = message.pos[0];
        g_state.peer_stand.pos[1] = message.pos[1];
        g_state.peer_stand.pos[2] = message.pos[2];
        g_state.peer_stand.yaw = message.yaw;
        g_state.peer_floor_rand_set = message.floor_rand_set != 0;
        g_state.peer_floor_rand = message.floor_rand;
        g_state.peer_floor_rand_map = message.floor_rand_map;
        g_state.peer_floor_rand_floor = message.floor_rand_floor;
        g_state.peer_floor_hold = message.floor_hold != 0;
        if ((message.busy == 1 || message.busy == 4) && !g_state.peer_busy && !g_state.peer_talk) {
            NetplayTestInputMark("peer-busy"); // coop-p2toan: a held --input script goes on (DC_TEST_INPUT_HOLD; l-local-play -HisBusy talk)
        }
        g_state.peer_busy = message.busy == 1; // coop-mayorhouse
        g_state.peer_scene = message.busy == 2; // coop-scenejoin
        g_state.peer_builds = message.busy == 3; // coop-buildarrive
        g_state.peer_talk = message.busy == 4; // coop-stage4 4e part 1
    }

    return true;
}

bool CoopWorldsMayLink() {
    if (!On() || !g_state.apart) {
        return true;
    }

    // The merge: both in the same town (each has heard the other there) and both say they are ready, so
    // neither starts the link and waits it out alone. The link's own start then brings the copies together
    // from the host's state.
    Zone here = Here();

    // Or the other copy has started the link already (its pads and state come in): it heard this one ready and linked, then
    // stalled in the lockstep waiting on this copy, sending no more presence, so a "ready" of its that came late or not at all
    // (lost, or overtaken by one saying "not ready" as its link put the bags home) held this copy back for good (w-world-split
    // -Case next-floor, 2026-10-25, one run in twenty: links A 2 B 1). DC_WORLDS_MUTATE=no_peer_linking (twin): its word alone.
    const bool peer_linking = NetplayPeerLinking() && !Mutated("no_peer_linking");
    NetplayHelloSaysStayer(); // coop-stayerhello: this copy's own link goes by its last Hello's stayer bit (HeardHello)

    // Test hook (DC_WORLDS_TEST_HOLD_TILL_PEER_DROP=1, coop-linkrace; w-world-split -LoneLink): this copy's first merge waits
    // until the other copy has linked alone (its link traffic heard) and given that link up (its Hellos under a new session),
    // so it links a moment after the other gave up, every run: the order that left the pair linking out of phase for good.
    if (static const bool hold = TestEnvOn("DC_WORLDS_TEST_HOLD_TILL_PEER_DROP"); hold) {
        static int           stage = 0; // 0 waits for its link, 1 for its drop, 2 done
        static std::uint32_t lone = 0;
        char                 line[200];

        if (stage == 0 && peer_linking) {
            stage = 1;
            lone = NetplayPeerHelloSession();
            std::snprintf(line, sizeof line, "test: player %d links alone (session %08x): this copy holds back till it gives up", 3 - Player(), (unsigned) lone);
            Log(line);
        }
        if (stage == 1 && NetplayPeerHelloSession() != lone && NetplayPeerHelloSession() != 0) {
            stage = 2;
            std::snprintf(line, sizeof line, "test: player %d gave its lone link up (Hello session %08x): this copy may link", 3 - Player(),
                          (unsigned) NetplayPeerHelloSession());
            Log(line);
        }
        if (stage < 2) {
            return false;
        }
    }

    // coop-apartsync: apart since a drop, and the other copy says Hello but nothing of its own apart play (no presence) for
    // 3 s: it came back new (it crashed or was closed, and joined again), so there is no play of its to keep; it links
    // as any join does, from the host's state. DC_WORLDS_MUTATE=no_fresh_rejoin (twin): it waits for a merge that never comes.
    // coop-joinfix (LAN playtest 2026-10-30, item 28): apart after a split too, not only after a drop. Her copy (apart, Mayor's
    // house) went away and came back from its title: his copy still had her as apart and held her new copy's Hellos back for
    // good ("held back a link: the copies are apart ... not in the peer's place").
    // coop-session (cause 4): her new copy's Hello says it has never linked (NetplayPeerFirstLink), so the session goes
    // Apart/Dropped -> Rejoining at once and she joins from the host's state, as any join; no wait on her silence. (A drop with
    // nothing heard of her since, the old rule, stays for a copy that came back before its first Hello said so.)
    // DC_WORLDS_MUTATE=fresh_rejoin_drop_only (twin): after a drop only, as before.
    const bool fresh = NetplayPeerFirstLink() && !Mutated("fresh_rejoin_drop_only") && !NetSessionOff();

    if (((g_state.dropped && !g_state.peer_heard) || fresh) && !Mutated("no_fresh_rejoin")) {
        const auto now = std::chrono::steady_clock::now();

        if (g_state.fresh_wait_since == std::chrono::steady_clock::time_point{} && !fresh) {
            g_state.fresh_wait_since = now;
        } else if (fresh || now - g_state.fresh_wait_since > std::chrono::seconds(3)) {
            char line[160];

            std::snprintf(line, sizeof line, "player %d came back as a new copy (%s): it joins from the host's state", 3 - Player(),
                          fresh ? "its Hello says it never linked" : "a Hello, no word of play apart");
            Log(line);
            NetSessionTo(NetSession::Rejoining, "a new copy of the other player");
            g_merge_state_in = false; // coop-localfix
            g_state.apart = false;
            g_state.dropped = false;
            FollowApart();
            return true;
        }
    }

    // coop-mayorhouse: both in one room, one of them in a talk or event there: no merge till it ends (BusyIndoors).
    if (const bool waits = here == g_state.peer && g_state.peer_heard && here.mode == GAME_MODE_EDIT && here.floor > 0 && (BusyIndoors() || g_state.peer_busy);
        waits != g_state.logged_busy_wait) {
        g_state.logged_busy_wait = waits;
        char line[200];

        if (waits) {
            std::snprintf(line, sizeof line, "merge waits: player %d is in a talk or event in %s (room mode %d here): this copy plays the room on its own till it ends",
                          BusyIndoors() ? Player() : 3 - Player(), EdInteriorName, CoopTownIndoorMode());
        } else {
            std::snprintf(line, sizeof line, "merge waits no longer: no talk or event in %s", EdInteriorName);
        }
        Log(line);
    }

    // coop-scenejoin: both in one room, one of them in a scene there: the merge goes into it (SceneIndoors).
    if (const bool into = here == g_state.peer && g_state.peer_heard && here.mode == GAME_MODE_EDIT && here.floor > 0 && (SceneIndoors() || g_state.peer_scene) &&
                          !BusyIndoors() && !g_state.peer_busy;
        into != g_state.logged_scene_merge) {
        g_state.logged_scene_merge = into;
        if (into) {
            char line[200];

            std::snprintf(line, sizeof line, "merge into a scene: player %d is in a scene in %s (room mode %d here): the merge goes into it, both play the rest of it",
                          SceneIndoors() ? Player() : 3 - Player(), EdInteriorName, CoopTownIndoorMode());
            Log(line);
        }
    }

    // coop-dunfreeze: both in one town, her own event apart there (BusyOutdoors): no merge till it ends.
    if (const bool waits = here == g_state.peer && g_state.peer_heard && here.mode == GAME_MODE_EDIT && here.floor < 0 && (BusyOutdoors() || g_state.peer_busy);
        waits != g_state.logged_town_busy_wait) {
        g_state.logged_town_busy_wait = waits;
        char line[200];
        char where[96];

        Describe(here, where, sizeof where);
        if (waits) {
            const int busy_player = BusyOutdoors() ? Player() : 3 - Player();

            std::snprintf(line, sizeof line, "merge waits: player %d is in %s own event %s, apart (town mode %d here): this copy plays the town on its own till it ends",
                          busy_player, busy_player == 2 ? "her" : "his", where, (int) GameMode);
        } else {
            std::snprintf(line, sizeof line, "merge waits no longer: no event of hers %s", where);
        }
        Log(line);
    }

    // coop-buildarrive: both in one town, the other building apart there (BuildsApart): no merge till it closes, this copy walks.
    if (const bool waits = here == g_state.peer && g_state.peer_heard && here.mode == GAME_MODE_EDIT && g_state.peer_builds;
        waits != g_state.logged_build_wait) {
        g_state.logged_build_wait = waits;
        char line[200];
        char where[96];

        Describe(here, where, sizeof where);
        std::snprintf(line, sizeof line, waits ? "merge waits: player %d builds apart %s (its Georama or menu): this copy walks the town on its own, not held black, till it closes"
                                               : "merge waits no longer: player %d builds no more %s",
                      3 - Player(), where);
        Log(line);
    }

    // coop-exitscene: outdoors too: both in one town, one of them in its event there (SceneOutdoors).
    if (const bool into = here == g_state.peer && g_state.peer_heard && here.mode == GAME_MODE_EDIT && here.floor < 0 && (SceneOutdoors() || g_state.peer_scene) &&
                          !BusyOutdoors() && !g_state.peer_busy;
        into != g_state.logged_town_scene_merge) {
        g_state.logged_town_scene_merge = into;
        if (into) {
            char line[200];
            char where[96];

            Describe(here, where, sizeof where);
            std::snprintf(line, sizeof line, "merge into a town scene: player %d is in an event %s (town mode %d here): the merge goes into it, both play the rest of it",
                          SceneOutdoors() ? Player() : 3 - Player(), where, (int) GameMode);
            Log(line);
        }
    }

    // coop-apartstats: and a word of the other's characters heard (its next presence brings one), unless it has linked.
    if (MergeReady() && g_state.sent_ready && (g_state.peer_ready || peer_linking) && (statsync::PeerHeard() || peer_linking)) {
        if (!g_state.peer_ready) {
            Log("the other copy has started the merge's link: this copy links too");
        }

        char where[96];
        char line[160];

        Describe(here, where, sizeof where);
        // coop-loadpull: his load's merge: his state as loaded, nothing of hers kept (no stand, no characters of hers).
        if (CoopWorldsLoadPull()) {
            char peer[96];

            Describe(g_state.peer, peer, sizeof peer);
            std::snprintf(line, sizeof line, "merging for player 1's load: this copy %s, player %d %s: both take his loaded state, in his zone", where,
                          3 - Player(), peer);
            Log(line);
            return true;
        }
        std::snprintf(line, sizeof line, "merging %s: player %d is here too", where, 3 - Player());
        Log(line);
        NetplayTiming("the merge starts");
        g_merging_split = g_state.split_tick;
        StandForMerge();
        statsync::Merge(); // coop-apartstats: her life, water and ailments for the characters she played

        // coop-catchuprace: DC_WORLDS_TEST_FLAG_AT_MERGE=F:V (the test's, player 2): her script sets game flag F to V now, and
        // its progress is held till the host's last blob is here (CoopWorldsTestSlotOnLastBlob), so it reaches him after that
        // blob every run.
        if (static const char *const flag_at = TestEnv("DC_WORLDS_TEST_FLAG_AT_MERGE"); flag_at != nullptr && Player() == 2 && SaveData != nullptr) {
            int flag = 0, value = 1;

            if (std::sscanf(flag_at, "%d:%d", &flag, &value) >= 1) {
                g_test_merge_started = true;
                g_state.test_progress_held = true;
                std::fprintf(stderr, "worlds: test: this copy (apart, player 2) sets game flag %d to %d as the merge starts; its progress held to the host's last blob\n",
                             flag, value);
                std::fflush(stderr);
                SaveData->SetGameFlag(flag, value);
            }
        }
        // DC_WORLDS_TEST_SLOT_AT_MERGE: its change now, and its record made before the link (and any catch-up) begins.
        if (static const bool at_merge = TestEnv("DC_WORLDS_TEST_SLOT_AT_MERGE") != nullptr; at_merge) {
            g_test_merge_started = true;
            slotsync::TestTick();
            slotsync::Tick();
        }

        return true;
    }

    auto now = std::chrono::steady_clock::now();

    // coop-small1b (his way out: both copies said ready within 20 ms, the merge started 430-470 ms later): what the merge still
    // waits for once this copy is ready, said when it changes (DC_COOP_DEBUG).
    if (CoopDebug()) {
        static int waits_was = -1;
        const int  waits = !(MergeReady() && g_state.sent_ready) ? 0 : (g_state.peer_ready || peer_linking ? 0 : 1) | (statsync::PeerHeard() || peer_linking ? 0 : 2);

        if (waits != waits_was) {
            waits_was = waits;
            if (waits != 0) {
                std::fprintf(stderr, "worlds: the merge waits for:%s%s [t %lld]\n", (waits & 1) ? " her ready" : "", (waits & 2) ? " a word of her characters" : "",
                             (long long) (std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 10000000));
            }
        }
    }

    if (now - g_state.held_logged > std::chrono::seconds(10)) {
        char line[260];

        g_state.held_logged = now;
        // coop-localfix: why not (the first condition MergeReady misses), for the logs.
        const Zone  here = Here();
        const char *why = MergeReady() ? "" : g_state.leaving ? ": leaving" : !g_state.progress_out.empty() ? ": progress unacknowledged"
                          : !slotsync::Idle() ? ": slot sync busy" : !g_state.peer_heard ? ": peer not heard" : here.mode == GAME_MODE_EDIT && ApartBuilds() && !CoopGeoramaCarries() && !Mutated("merge_in_georama") ? ": this copy builds (Georama)" : here.mode == GAME_MODE_EDIT && ApartHerMenu() && !CoopTownMenuCarries() ? ": her menu is open" : !(here == g_state.peer) ? ": not in the peer's place"
                          : !g_state.peer_sees_set || !(g_state.peer_sees == here) ? ": the peer does not see this place"
                          : here.mode == GAME_MODE_DUNGEON && !g_floor_played ? ": the floor has not played yet" : ": other";
        std::snprintf(line, sizeof line, "held back a link: the copies are apart (player %d's Hello; this copy %s%s, player %d %s)", 3 - Player(),
                      MergeReady() ? "ready to merge" : "not ready", why, 3 - Player(), g_state.peer_ready ? "ready" : "not ready");
        Log(line);
    }

    return false;
}

static void LeaveFloorAsMember(); // coop-escapeface (below CoopWorldsStayOnFloor)

bool CoopWorldsStorySceneLeaves();
static bool g_script_exit = false; // coop-pt1109f

bool CoopWorldsTakeExit(int next_map) {
    if (!On() || !g_state.up || NetplayLocalPlayer() == 0 || g_state.apart) {
        return true;
    }

    // A floor change (a jump to the dungeon this copy is in) keeps both together as before: the floor split,
    // with the merge on the next floor, comes later.
    Zone to = Target(next_map);

    if (mode == GAME_MODE_DUNGEON && to.mode == GAME_MODE_DUNGEON && to.map == selectMapNo && !Mutated("split_floors")) {
        std::fprintf(stderr, "worlds: floor change at link tick %lld (map %d): both go on together (floor changes do not split yet)\n",
                     (long long) g_state.tick, next_map);
        std::fflush(stderr);
        return true;
    }

    // coop-stage4 4e part 2 (Jep 11-07: a cutscene that leaves town takes everyone watching with it): her event carried through
    // her merge leaves town: both copies go, together.
    if (CoopTownSwapDeferred()) {
        std::fprintf(stderr, "worlds: her carried event leaves town for map %d at link tick %lld: both copies go\n", next_map, (long long) g_state.tick);
        std::fflush(stderr);
        return true;
    }
    // coop-pt1109f: the same rule for his story scene: only hers was covered (CoopTownSwapDeferred), so his, with her watching it, split at its jump and left her in town.
    // DC_WORLDS_MUTATE=scene_exit_splits (twin): the split at its jump, as before.
    if (CoopWorldsStorySceneLeaves() && !Mutated("scene_exit_splits")) {
        std::fprintf(stderr, "worlds: the story scene %d leaves town for map %d at link tick %lld: both copies go\n", (int) EdEventInfo.event_no, next_map, (long long) g_state.tick);
        std::fflush(stderr);
        return true;
    }

    // The exit's player leaves (player 1's, or player 2's when her partner took the town's exit), the other copy stays
    // (both copies are here on the same tick).
    const int leaver = g_town_starter;
    bool      stays = NetplayLocalPlayer() != leaver && !Mutated("no_split");

    g_town_starter = 1;
    SplitTo(Target(next_map), next_map, stays, leaver);
    if (!stays) {
        LeaveFloorAsMember();
    }
    return !stays;
}


void CoopWorldsInteriorStarted() {
    NetplayInteriorStarted();
}

// coop-doorgate (Jep, 2026-10-28, "gather first"): a room whose arrival still has a story to play (its arrival, 128, can set a game
// flag that is not set yet: the mayor's first visit) is gone into by both players together or not at all. Its door, used
// while the other player is linked but not beside it, does not open: the player keeps walking with a note ("Needs both
// players"), and the other player's screen gets a call to come. Both at the door (the other player's character beside the
// one who opened it), both go in linked and the arrival's scene plays on both in lockstep (coop-scenejoin's together path).
// Either player may ask, together or apart. Norune's cave door counts too while B1's opening scene is unseen (its event 130,
// Cross or Square: CaveRefused). Events that start by themselves are never gated. game.coop_story_gather false (the host's):
// the doors as before. DC_WORLDS_MUTATE=door_gate_off (twin): no gate.
namespace {
namespace doorgate {

// A room of a town (gedit/e0T/in/<name>) whose arrival can set constant game flags: its name and those flags.
struct Room {
    char name[16] = "";
    int  town = 0; // 1 to 5
    int  flags[8] = {};
    int  count = 0;
    int  parts[4] = {}; // coop-pt1109c: the Georama parts whose finished scene its arrival plays once (_CHECK_COMPLETE_PARTS, then _SSET_CMP_EVENT)
    int  part_count = 0;
};

std::vector<Room> g_rooms;
bool              g_scanned = false;

// The other player's character this near the one who opens a gated door (units): both are at the door.
constexpr float kBeside = 100.0f;

const char *const kTowns[5] = {"Norune", "Matataki", "Queens", "Muska Racka", "Yellow Drops"};

// Each room's arrival script read once, every room of the five towns (the rooms are named i0Th01 ... and i0Tc01 ...: each
// name tried, the ones with a script read), at the first town tick. The room's own script is loaded only with the room, so
// the door could not know it otherwise.
void Scan() {
    if (g_scanned) {
        return;
    }
    g_scanned = true;

    std::vector<char> buffer;
    int               scripts = 0;

    for (int town = 1; town <= 5; town++) {
        for (const char *pattern : {"i%02dh%02d", "i%02dh%02d1", "i%02dc%02d"}) {
            for (int n = 1; n <= 20; n++) {
                Room room;
                char path[64];

                room.town = town;
                std::snprintf(room.name, sizeof room.name, pattern, town, n);
                std::snprintf(path, sizeof path, "gedit/e%02d/in/%s/event.stb", town, room.name);

                const std::uintmax_t size = dc::data::DataFileSize(path);
                int                  got = 0;

                if (size < sizeof(RS_PROG_HEADER) || size > (16u << 20)) {
                    continue;
                }
                buffer.assign(((std::size_t) size / 2048 + 2) * 2048, 0); // (the read pads its last sector with zeros)
                if (LoadFile2(path, buffer.data(), &got, 0) == 0) {
                    continue;
                }
                scripts++;
                int       flags[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
                const int sets = std::min(ScriptExtCalls(reinterpret_cast<const RS_PROG_HEADER *>(buffer.data()), 128, 700, flags, 8), 8); // _SSET_GAME_FLAG

                for (int i = 0; i < sets; i++) {
                    if (flags[i] >= 0) {
                        room.flags[room.count++] = flags[i];
                    }
                }
                // coop-pt1109c (Jep's 11-09 playtest 1l: "P2 entered the finished house: the scene started with no ask to P1"): a house's finished scene is a story the
                // room's arrival (128) plays once, with no game flag: if its part is complete and its completion event unset, the scene, an item (_SITEM_GET: i01h02's 114,
                // the Stone Breaker), and _SSET_CMP_EVENT (ext 260). Read from the room scripts: 5 of Norune's 7 rooms go this way. This table looked for game flags (700)
                // alone, so those doors opened for one player. They now ask as any other room with a story. DC_WORLDS_MUTATE=door_finished_off (twin): as before.
                if (!Mutated("door_finished_off")) {
                    int parts[4] = {-1, -1, -1, -1};
                    const int cmps = std::min(ScriptExtCalls(reinterpret_cast<const RS_PROG_HEADER *>(buffer.data()), 128, 260, parts, 4), 4); // _SSET_CMP_EVENT

                    for (int i = 0; i < cmps; i++) {
                        bool seen = parts[i] < 0;

                        for (int j = 0; j < room.part_count; j++) {
                            seen = seen || room.parts[j] == parts[i];
                        }
                        if (!seen) {
                            room.parts[room.part_count++] = parts[i];
                        }
                    }
                }
                if (room.count > 0 || room.part_count > 0) {
                    g_rooms.push_back(room);
                    std::fprintf(stderr, "worlds: doorgate: table: %s in %s, its arrival sets game flag(s) %d%s, finished part %d\n", room.name, kTowns[town - 1], room.count > 0 ? room.flags[0] : -1,
                                 room.count > 1 ? " and more" : "", room.part_count > 0 ? room.parts[0] : -1);
                }
            }
        }
    }

    std::fprintf(stderr, "worlds: doorgate: table made: %d room script(s) read, %d room(s) whose arrival has a story\n", scripts, (int) g_rooms.size());
    std::fflush(stderr);
}

int Find(const char *name) {
    Scan();
    for (int i = 0; name != nullptr && i < (int) g_rooms.size(); i++) {
        if (std::strcmp(g_rooms[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

// The room's first story flag still unset (its story to come), or -1.
int Unset(int row) {
    if (row < 0 || row >= (int) g_rooms.size() || SaveData == nullptr) {
        return -1;
    }
    for (int i = 0; i < g_rooms[row].count; i++) {
        if (SaveData->GetGameFlag(g_rooms[row].flags[i]) == 0) {
            return g_rooms[row].flags[i];
        }
    }
    for (int i = 0; i < g_rooms[row].part_count; i++) { // coop-pt1109c: a finished scene still to play (said as 10000 + its part)
        if (EditPartsInfo.CheckComplete(g_rooms[row].parts[i]) != 0 && EditPartsInfo.GetCompEvent(g_rooms[row].parts[i]) == 0) {
            return 10000 + g_rooms[row].parts[i];
        }
    }
    return -1;
}

const char *RoomName(int row) {
    static char name[64];

    if (row >= 0 && row < (int) g_rooms.size() && std::strcmp(g_rooms[row].name, "i01h06") == 0) {
        return "the mayor's house";
    }
    std::snprintf(name, sizeof name, "a house in %s", row >= 0 && row < (int) g_rooms.size() ? kTowns[g_rooms[row].town - 1] : "town");
    return name;
}

const char *TownName(int row) {
    return row >= 0 && row < (int) g_rooms.size() ? kTowns[g_rooms[row].town - 1] : "town";
}

// What each player's screen says, for 4 s (CoopWorldsStatus): the asker's note, the other's call (together; apart the
// call comes as a notice, kWhatDoorCall).
struct Note {
    char                                  text[160] = "";
    std::chrono::steady_clock::time_point at{};
};

Note g_note[2];

// coop-gatepopup (Jep, 2026-10-28: the asker of a no, an unanswered call or a busy player is told "in the same style as the player
// connected / disconnected pop-up"): an answer as the join notice (netplay's SetNotice: the game's lettering at the top left, a
// few seconds), on that player's copy alone, not on the link line. False with DC_WORLDS_MUTATE=answer_link_line (twin): the link
// line, as before.
bool AnswerNotice(int player, const char *text) {
    if (Mutated("answer_link_line")) {
        return false;
    }
    if (player == Player()) {
        NetplayShowNotice(text);
        std::fprintf(stderr, "worlds: answer notice on this copy (player %d): \"%s\"\n", player, text);
        std::fflush(stderr);
    }
    return true;
}

// notice: an answer (AnswerNotice), else the link line's note.
void SetNote(int player, const char *text, bool notice = false) {
    if (player < 1 || player > 2) {
        return;
    }
    std::fprintf(stderr, "worlds: doorgate: note for player %d: \"%s\"\n", player, text);
    std::fflush(stderr);
    if (notice && AnswerNotice(player, text)) {
        g_note[player - 1].text[0] = 0; // (the call's "has been called" off the link line too)
        return;
    }
    std::snprintf(g_note[player - 1].text, sizeof g_note[player - 1].text, "%s", text);
    g_note[player - 1].at = std::chrono::steady_clock::now();
}

const char *NoteFor(int player) {
    if (player < 1 || player > 2 || g_note[player - 1].text[0] == 0 || std::chrono::steady_clock::now() - g_note[player - 1].at > std::chrono::seconds(4)) {
        return nullptr;
    }
    return g_note[player - 1].text;
}

// The other player is linked: together, or apart and heard from in the last 5 s (a player gone from the link holds nobody back).
bool OtherLinked() {
    if (!On() || Player() == 0) { // (Player: the copy's role; NetplayLocalPlayer is 0 while apart, the link down)
        return false;
    }
    if (g_state.up && !g_state.apart) {
        return true;
    }
    return g_state.apart && g_state.peer_heard && std::chrono::steady_clock::now() - g_state.peer_at < std::chrono::seconds(5);
}

// Together: the other player's character beside the one at the door (both copies alike: the partner's place is lockstep state).
bool BothAtDoor() {
    float partner[3];
    float toan[4];

    if (g_state.apart || !g_state.up || Chara == nullptr || !coop::TownPartnerShown(partner, nullptr)) {
        return false;
    }
    Chara->GetPosition(toan);

    const float dx = partner[0] - toan[0];
    const float dz = partner[2] - toan[2];

    return dx * dx + dz * dz < kBeside * kBeside && !Mutated("door_gate_apart_only");
}

// coop-doorgate: a Georama "Event" pick's story scene goes as a door's row of its own: -2 - the town (0 Norune ... 4).
bool GeoramaRow(int row) {
    return row <= -2 && row >= -6;
}

const char *GeoramaTown(int row) {
    return kTowns[-2 - row];
}

const char *CallText(int player, int row, bool cave) {
    static char text[160];

    if (GeoramaRow(row)) {
        std::snprintf(text, sizeof text, "Player %d wants to watch a story scene in %s: come to %s", player, GeoramaTown(row), GeoramaTown(row));
    } else if (cave) {
        std::snprintf(text, sizeof text, "Player %d wants to enter the Divine Beast Cave: come to the cave door in Norune", player);
    } else {
        std::snprintf(text, sizeof text, "Player %d wants to enter %s: come to %s", player, row >= 0 ? RoomName(row) : "a house", TownName(row));
    }
    return text;
}

// The call as a question in the game's own message window with the hand (Jep, 2026-10-28: "use the in game UI ... a yes no
// option you can pick with the hand cursor"; coop_useprompt's window, as the floor gate's): the asked player picks "Come"
// or "Not now" (Up and Down move the hand, Cross picks, Circle is "Not now"); the asker is told the answer. Together it is
// stepped on both copies alike from the asked player's agreed pad (its open state holds that pad's keys back from the game);
// apart on the asked copy alone, its answer sent back (kWhatDoorAnswer). At most kAskTicks with no answer.
constexpr int kAskTicks = 1200;

// coop-gatepopup (Jep, 2026-10-28: "some sort of protection against an immediate accidental input from someone having been about
// to press x and then the prompt shows up"): every question of the gate (the door's call, the floor gate's vote, the floor call)
// takes no answer in its first kAnswerGrace ticks (half a second): what was held as it opened counts only once let go, as before,
// and a press on its way then counts for nothing. DC_WORLDS_MUTATE=answer_grace_off (twin): taken at once.
constexpr int kAnswerGrace = 30;

bool InGrace(int age) {
    return age < kAnswerGrace && !Mutated("answer_grace_off");
}

// coop-gatepopup: apart, the asker of a call waits for its answer (held as in a pause, CoopWorldsQuestionHolds) until it comes,
// at most kAskTicks' time and a little: the other copy answers within that (its own question times out).
std::chrono::steady_clock::time_point g_wait_answer{};

// The call this copy sent apart and waits on (what and a to d as sent), its Circle's pad and age (WaitCircle).
struct SentCall {
    int           what = 0;
    int           a = 0, b = 0, c = 0, d = 0;
    std::uint16_t held = 0xFFFF;
    int           age = 0;
};

SentCall g_sent;

void WaitForAnswer(bool wait) {
    g_wait_answer = wait ? std::chrono::steady_clock::now() + std::chrono::seconds(kAskTicks / 60 + 2) : std::chrono::steady_clock::time_point{};
    g_sent.held = 0xFFFF;
    g_sent.age = 0;
    std::fprintf(stderr, "worlds: question: this copy (player %d) %s\n", Player(), wait ? "waits for the other player's answer" : "has its answer");
    std::fflush(stderr);
}

bool Waiting() {
    return g_wait_answer != std::chrono::steady_clock::time_point{};
}

// coop-gatepopup (Jep, 2026-10-28: the asker's box says it waits "and have an indication that you can press the cancel button, or
// circle on a ps4 controller, to cancel the invite"): apart, the waiting asker's Circle (this copy's own pad, past the grace)
// withdraws the call sent: the other copy's question closes (kWhatDoorCall c 1, kWhatCall b 1) and its player is told.
// DC_WORLDS_MUTATE=call_cancel_off (twin): no cancel.
void WaitCircle(std::uint16_t now) {
    if (!Waiting() || g_sent.what == 0) {
        return;
    }

    const std::uint16_t pressed = InGrace(g_sent.age++) ? 0 : now & ~g_sent.held;

    g_sent.held = now;
    if ((pressed & kInputCircle) == 0 || Mutated("call_cancel_off")) {
        return;
    }

    {
        coop::SoundOwner sound(Player());

        SndSePlay(MENU_SOUND_REFUSE, -1, 0);
    }
    if (g_sent.what == kWhatDoorCall) {
        QueueProgress(kWhatDoorCall, g_sent.a, g_sent.b, 1, 0);
    } else {
        QueueProgress(kWhatCall, g_sent.a, 1, g_sent.c, g_sent.d);
    }
    std::fprintf(stderr, "worlds: question: this copy (player %d) cancels its call (Circle)\n", Player());
    std::fflush(stderr);
    g_sent = SentCall{};
    WaitForAnswer(false);
}
// coop-gatevote (Jep, 2026-11-02: "if p2 hits no it has something like a cooldown before you can try the join request again"):
// after an answer (or a withdrawal) the same door asks again at the asker's next Cross, once the Cross held through the
// question is let go (else the press the hold kept from the game reaches its door as the question closes). It waited 300 ticks
// (5 s) before, the asker's Crosses meanwhile told "has been called" and asked nothing. DC_WORLDS_MUTATE=door_ask_cooldown
// (twin): the 300 ticks, as before.
bool g_release_cross = false; // the asker's Cross not yet let go since the last answer (CoopWorldsDoorQuestionPad clears it)

struct Question {
    bool          open = false;
    bool          apart = false; // asked on this copy alone (the call came as progress)
    int           asked = 0;     // 1 or 2: whose screen and pad
    int           asker = 0;
    int           row = -1;
    bool          cave = false;
    int           cursor = 0; // 0 Come, 1 Not now
    int           ticks = 0;  // steps left
    int           age = 0;    // steps since it opened (coop-gatepopup: kAnswerGrace)
    std::uint16_t held = 0xFFFF;
    std::uint16_t asker_held = 0xFFFF; // coop-gatepopup: the asker's pad, for its Circle (cancel)
    int           answered_for = -2; // the door last answered, and when (steps counted by Step)
    std::int64_t  answered_at = -100000;
    std::int64_t  steps = 0;
};

Question g_question;

// coop-gatepopup3 (Jep's 2026-10-28 Steam run: P1 at the mayor's door got "Player 2 is busy" with her doing nothing; his log:
// "player 2 is busy (mode 1)"): together the town's GameMode is Toan's walk (his door's own step had moved it), never hers.
// She is free while her pad is hers to walk with (no menu, pause, Georama or wheel of hers: CoopTownHerPadFree) and no room
// event of hers runs. Test hook DC_WORLDS_TEST_TOWN_BUSY_AT_ASK=1 (both copies): the town's mode reads busy at the ask.
// DC_WORLDS_MUTATE=busy_reads_town_mode (twin): her freedom by the town's mode too, as before.
bool HerFreeTogether() {
    static const bool town_busy = TestEnvOn("DC_WORLDS_TEST_TOWN_BUSY_AT_ASK");
    const bool        town_walks = ::GameMode == ED_MODE_WALK && !town_busy;

    if (Mutated("busy_reads_town_mode")) {
        return town_walks && CoopTownHerPadFree();
    }
    if (town_busy) {
        std::fprintf(stderr, "worlds: doorgate: test: the town's mode reads busy at the ask (Toan's), her pad free %d\n", (int) CoopTownHerPadFree());
        std::fflush(stderr);
    }
    return CoopTownHerPadFree() && !CoopTownHerRoomEventOn();
}

void Ask(int asked, int asker, int row, bool cave, bool apart) {
    Question  &q = g_question;
    const int  door = cave ? -1 : row;

    if (q.open || (q.answered_for == door && (Mutated("door_ask_cooldown") ? q.steps - q.answered_at < 300 : g_release_cross))) {
        return;
    }
    if (q.answered_for == door) {
        char again[96];

        std::snprintf(again, sizeof again, "doorgate: the same door asks again %lld steps after its last answer", (long long) (q.steps - q.answered_at));
        Log(again);
    }

    // Jep (2026-10-28): never over a cutscene, a minigame or a menu: a player not free is not asked, the answer is "busy".
    // Together both copies alike (the town's mode, the menus and her pad are lockstep state); apart this copy's own player
    // (CoopWorldsPlayerFree, floorgate's). DC_WORLDS_MUTATE=door_busy_asked (twin): asked anyway.
    const bool free = apart ? CoopWorldsPlayerFree()
                            : !coop::FullMenuOpenFor(asked) && (asked == 2 ? HerFreeTogether() : ::GameMode == ED_MODE_WALK &&
                                  (EdSystemMesCheck() == 0 || Mutated("talk_not_busy"))); // (coop-gatepopup2: a talk's window is up)

    q.asked = asked;
    q.asker = asker;
    q.row = row;
    q.cave = cave;
    q.apart = apart;
    if (!free && !Mutated("door_busy_asked")) {
        std::fprintf(stderr, "worlds: doorgate: player %d is busy (mode %d): not asked\n", asked, (int) ::GameMode);
        std::fflush(stderr);
        Close(2);
        return;
    }
    q.open = true;
    q.apart = apart;
    q.asked = asked;
    q.asker = asker;
    q.row = row;
    q.cave = cave;
    q.cursor = 0;
    q.ticks = kAskTicks;
    q.age = 0;
    q.held = 0xFFFF;
    q.asker_held = 0xFFFF; // (what the asked player holds as it opens counts only once let go)
    std::fprintf(stderr, "worlds: doorgate: player %d asked on %s: player %d wants to go into %s\n", asked, apart ? "this copy alone" : "both copies", asker,
                 cave ? "the Divine Beast Cave" : GeoramaRow(row) ? "a Georama story scene" : RoomName(row));
    std::fflush(stderr);
}

// The door did not open for `asker`: the asker's note, the other's call (together on this copy's own notes, both copies
// alike; apart a progress for the other copy, once and then again only after 10 s for the same door).
void Refuse(int asker, int row, bool cave, int flag) {
    static std::chrono::steady_clock::time_point called{};
    static int                                   called_for = -2;
    const auto                                   now = std::chrono::steady_clock::now();
    const int                                    other = 3 - asker;
    char                                         line[200];

    std::snprintf(line, sizeof line, "Needs both players. Player %d has been called", other);
    SetNote(asker, line);
    std::snprintf(line, sizeof line, "doorgate: player %d's door into %s does not open (game flag %d unset: its story needs both players), %s; player %d walks on",
                  asker, cave ? "the cave" : GeoramaRow(row) ? "a Georama scene" : g_rooms[row].name, flag, g_state.apart ? "apart" : "together, the other not beside it", asker);
    Log(line);

    const int door = cave ? -1 : row;

    if (!g_state.apart) {
        if (Mutated("door_question_hud")) {
            SetNote(other, CallText(asker, row, cave)); // DC_WORLDS_MUTATE=door_question_hud (twin): the call as a link-line note, as first built
        } else if (!Mutated("door_no_call")) {
            Ask(other, asker, row, cave, false);
        }
    } else if (called_for != door || now - called > std::chrono::seconds(10) || (!Waiting() && !g_release_cross && !Mutated("door_ask_cooldown"))) { // (coop-gatevote: answered, it calls again)
        called_for = door;
        called = now;
        QueueProgress(kWhatDoorCall, cave ? 0 : row, cave ? 1 : 0, 0, 0);
        WaitForAnswer(true); // coop-gatepopup
        g_sent = SentCall{kWhatDoorCall, cave ? 0 : row, cave ? 1 : 0, 0, 0};
    }
}

// A door GotoInterior was told to take (CoopWorldsTakeInterior) that the gate keeps shut: GotoInterior returns before it
// changes anything, and the town's CoopWorldsStayInTown after it does nothing (the asker walks on where they stand).
bool g_refused = false;

// The mod's own ways in (a catch-up's door to the host's building): never gated.
bool g_mod_door = false;

bool Gated() {
    return CoopWorldsStoryGather() && !Mutated("door_gate_off") && OtherLinked() && mode == GAME_MODE_EDIT && EdInteriorFlag == 0 && !g_reentering &&
           !g_mod_door && !NetplayCatchUnderWay();
}

// The player using a door: apart this copy's own; together the one who opened it (g_town_starter).
int Asker() {
    return g_state.apart ? Player() : (g_town_starter == 2 ? 2 : 1);
}

// Whether the door into room `name` stays shut for `asker` (kShut), opens for both together (kBoth: both at it), or opens as
// before (kOpen).
enum { kOpen = 0, kShut = 1, kBoth = 2 };

int DoorFor(const char *name, int asker) {
    if (!Gated()) {
        return kOpen;
    }

    const int row = Find(name);
    const int flag = Unset(row);

    if (flag < 0) {
        return kOpen;
    }

    if (BothAtDoor()) {
        char line[200];

        std::snprintf(line, sizeof line, "doorgate: both players at the door into %s (game flag %d unset): both go in together, player %d opened it",
                      name, flag, asker);
        Log(line);
        return kBoth;
    }

    Refuse(asker, row, false, flag);
    return kShut;
}

} // namespace doorgate
} // namespace

bool CoopWorldsDoorRefused(const char *name, int asker) {
    return doorgate::DoorFor(name, asker) == doorgate::kShut;
}

// coop-doorgate (Jep, 2026-10-28: a Georama "Event" with a story is watched by both, as a door's room): the town's event the
// Georama menu's Event pick would fade to, while it can still set a story flag this copy has not set (its constant
// _SSET_GAME_FLAGs, 700) or add a character (710), apart from a linked partner: no fade, the asker told, the other called.
// Together it plays for both, on both screens, as before. DC_WORLDS_MUTATE=georama_gate_off (twin): no gate.
bool CoopWorldsGeoramaEventRefused(int event_no) {
    if (!doorgate::Gated() || !g_state.apart || Mutated("georama_gate_off") || EdEventData == nullptr || event_no <= 0 || SaveData == nullptr ||
        MapNo < 0 || MapNo > 4) {
        return false;
    }

    const auto *prog = static_cast<const RS_PROG_HEADER *>(static_cast<const void *>(EdEventData));
    int         flags[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    const int   sets = std::min(ScriptExtCalls(prog, event_no, 700, flags, 8), 8);
    int         unset = -1;

    for (int i = 0; i < sets && unset < 0; i++) {
        unset = flags[i] >= 0 && SaveData->GetGameFlag(flags[i]) == 0 ? flags[i] : -1;
    }
    if (unset < 0 && ScriptExtCalls(prog, event_no, 710, nullptr, 0) == 0) {
        return false;
    }

    std::fprintf(stderr, "worlds: doorgate: player %d's Georama event %d (game flag %d unset) needs both players\n", Player(), event_no, unset);
    std::fflush(stderr);
    doorgate::Refuse(Player(), -2 - (int) MapNo, false, unset);
    return true;
}

bool CoopWorldsTakeInterior(const char *name) {
    // coop-doorgate (above): a room whose story needs both players.
    if (const int door = doorgate::DoorFor(name, doorgate::Asker()); door == doorgate::kShut) {
        doorgate::g_refused = true;
        g_town_starter = 1;
        return false;
    } else if (door == doorgate::kBoth) {
        g_town_starter = 1;
        g_door_yes_building = 0; // coop-gatepopup: in by the door itself first (his Cross beside her, once she was put there)
        return true;
    }

    // A door into a building, both copies on the same tick: the player who opened it goes in (player 1's walking,
    // or player 2's partner: CoopWorldsTownEventBy), the other copy stays outside, and the copies are apart until the
    // one inside comes back out (the town's merge). DC_WORLDS_MUTATE=no_door_split: both go in, as before (twin).
    if (!On() || !g_state.up || NetplayLocalPlayer() == 0 || g_state.apart || name == nullptr || Mutated("no_door_split")) {
        return true;
    }

    // Both going back into the building they merged in: together, no split.
    if (g_reentering) {
        g_reentering = false;
        g_town_starter = 1;
        Log("both go back into the building they merged in");
        return true;
    }

    // coop-localfix (Jep's 2026-10-26 playtest: "if player 1 is standing in front of the door to the mayors house, and P2 presses A
    // to enter, P1 and P2 both enter"; "same with exiting the house, same if P1 initiated the door opening"): the door is the
    // opener's alone, whoever stands beside it. DC_WORLDS_MUTATE=door_together (twin, and a test set-up): both at the door
    // (the other player's character beside the one who opened it) go in together, linked, as before.
    float partner[3];
    float toan[4];

    if (coop::TownPartnerShown(partner, nullptr) && Chara != nullptr && Mutated("door_together")) {
        Chara->GetPosition(toan);
        const float dx = partner[0] - toan[0];
        const float dz = partner[2] - toan[2];

        if (dx * dx + dz * dz < kDoorTogether * kDoorTogether) {
            g_town_starter = 1;
            Log("both go in by the door together (the other player beside it)");
            return true;
        }
    }

    Zone to;

    to.mode = GAME_MODE_EDIT;
    to.map = MapNo;
    to.floor = InteriorId(name);

    const int leaver = g_town_starter;
    const bool stays = NetplayLocalPlayer() != leaver;

    g_town_starter = 1;
    SplitTo(to, -1, stays, leaver);
    return !stays;
}

// coop-pt1106-batch (Jep's 2026-11-06 playtest, item 4: "when either player leaves the mayor's house, they vanish at once on the
// other player's screen instead of playing the door-opening animation first"): together in a room, a door taken by one player
// split the pair on its press, so the other copy dropped the leaver at once. Now the door opens on both copies first (the
// leaver's open motion, its sound; the other walks on, the fade drawn out of sight on their copy) and the copies split as it
// ends, each then as before (the leaver's copy goes out, the other stays as its player). Whenever the split would: linked,
// together, inside, the other player shown and not going out with them. DC_WORLDS_MUTATE=door_split_at_press (twin): the
// split on the press, as before.
namespace {
struct DoorWindow {
    int          player = 0;
    std::int64_t from = 0;
    float        other[3] = {}; // where the other player stood as it began
};

// Where the player who is not the leaver stands now (her partner, or Toan).
void DoorWindowOther(int leaver, float *at) {
    float partner[3] = {};
    float toan[4] = {};

    if (leaver == 1) {
        coop::TownPartnerShown(partner, nullptr);
        std::copy(partner, partner + 3, at);
    } else if (Chara != nullptr) {
        Chara->GetPosition(toan);
        std::copy(toan, toan + 3, at);
    }
}

DoorWindow g_door_window;
} // namespace

bool CoopWorldsDoorWindowStart(int player) {
    if (!On() || !g_state.up || Player() == 0 || g_state.apart || EdInteriorFlag == 0 || Mutated("no_building_split") ||
        Mutated("door_split_at_press") || g_door_window.player != 0) {
        return false;
    }

    float partner[3];
    float toan[4];

    if (!coop::TownPartnerShown(partner, nullptr) || Chara == nullptr) {
        return false; // (her character not inside: both go out, as before)
    }
    Chara->GetPosition(toan);
    if (Mutated("door_together") && (partner[0] - toan[0]) * (partner[0] - toan[0]) + (partner[2] - toan[2]) * (partner[2] - toan[2]) < kDoorTogether * kDoorTogether) {
        return false; // (both go out together: no split)
    }

    g_door_window.player = player;
    g_door_window.from = g_state.tick;
    DoorWindowOther(player, g_door_window.other);
    std::fprintf(stderr, "worlds: player %d's door out of the room opens together at link tick %lld (this copy player %d); the split comes as it ends\n", player,
                 (long long) g_state.tick, Player());
    std::fflush(stderr);
    return true;
}

int CoopWorldsDoorWindowBy() {
    if (g_door_window.player != 0 && (!g_state.up || g_state.apart || EdInteriorFlag == 0)) {
        std::fprintf(stderr, "worlds: player %d's door window dropped (the link or the room changed) at link tick %lld\n", g_door_window.player,
                     (long long) g_state.tick);
        g_door_window = DoorWindow{};
    }
    return g_door_window.player;
}

void CoopWorldsDoorWindowEnd() {
    if (g_door_window.player == 0) {
        return;
    }
    float now[3] = {};

    DoorWindowOther(g_door_window.player, now);
    std::fprintf(stderr, "worlds: player %d's door opened together for %lld ticks: the copies split now (link tick %lld); the other walked %.1f through it, its fade %d frames out of sight on this copy (player %d)\n",
                 g_door_window.player, (long long) (g_state.tick - g_door_window.from), (long long) g_state.tick,
                 std::hypot(now[0] - g_door_window.other[0], now[2] - g_door_window.other[2]), CoopTownDoorFadeHiddenTake(), Player());
    std::fflush(stderr);
    g_door_window = DoorWindow{};
}

bool CoopWorldsLeaveBuildingBy(int player) {
    // A door out of a building the pair is in together, both copies on the same tick (Jep's 2026-10-24 rule: one player
    // must never hold the other): the player who took it goes out, the other stays inside, and the copies are apart
    // until they meet again (the town's merge, or the building's). Beside each other at the door, both go out.
    // DC_WORLDS_MUTATE=no_building_split (twin): both go out whoever took it, as before.
    if (!On() || !g_state.up || NetplayLocalPlayer() == 0 || g_state.apart || EdInteriorFlag == 0 || Mutated("no_building_split")) {
        return true;
    }

    float partner[3];
    float toan[4];

    // coop-localfix: beside each other at the door, only the one who took it goes out (DC_WORLDS_MUTATE=door_together: both, as before).
    if (coop::TownPartnerShown(partner, nullptr) && Chara != nullptr) {
        Chara->GetPosition(toan);
        const float dx = partner[0] - toan[0];
        const float dz = partner[2] - toan[2];

        if (dx * dx + dz * dz < kDoorTogether * kDoorTogether && Mutated("door_together")) {
            Log(player == 1 ? "both go out of the building together (player 2 beside player 1)" : "both go out of the building together (player 1 beside player 2)");
            return true;
        }
    } else {
        // Her character not inside (not loaded yet): as before, both go out.
        return true;
    }

    Zone to;

    to.mode = GAME_MODE_EDIT;
    to.map = MapNo;

    const bool stays = NetplayLocalPlayer() != player;

    SplitTo(to, -1, stays, player); // (the other's place, the building, from Here() before this copy counts as out)
    g_leaving_building = !stays;

    // Player 2's copy staying inside: its player is hers now and stands where she stood.
    if (stays && Player() == 2) {
        float pos[3] = {};
        bool  placed = coop::TownPlacePlayerAtPartner(pos);
        char  line[160];

        std::snprintf(line, sizeof line, "stays in the building %s as Toan at (%.0f %.0f %.0f)%s", EdInteriorName, pos[0], pos[1], pos[2],
                      placed ? " (where player 2 stood)" : "; player 2 was not inside, Toan stays put");
        Log(line);
    } else if (stays) {
        Log("stays in the building as Toan where he stood (player 2 went out)");
    }

    return !stays;
}

bool CoopWorldsPartnerMayStart(const void *program, int event_no) {
    // Player 2's partner on a town polygon whose event leaves the town by its script's own _MAP_JUMP (Norune's
    // entrance): she takes it, as Toan would (Jep's 2026-10-21 rule: player 2 can always use things, but for
    // character-locked content and story prompts). A door into a building (_GOTO_INTERIOR) stays Toan's: the copies
    // do not split into a building. DC_WORLDS_MUTATE=no_partner_exit (twin): only Toan's walking starts them.
    if (!On() || !g_state.up || g_state.apart || NetplayLocalPlayer() == 0) {
        return false;
    }

    // No program: only whether she may (linked and together), for her doors. coop-doorprompt: not with her picker at the
    // cave door open or closing this tick (its Cross is the picker's: as her door's it took both copies into the cave).
    // coop-menucross: nor while her menu, pause, Georama or wheel has her pad (CoopTownHerPadFree).
    if (program == nullptr) {
        return (!coop::TownDoorPickHeldFor(2) || Mutated("door_pick_cross")) && CoopTownHerPadFree() && !CoopWorldsDoorAsks(2); // (coop-doorgate: her Cross answers it)
    }

    if (event_no <= 0 || Mutated("no_partner_exit")) {
        return false;
    }

    const auto *prog = static_cast<const RS_PROG_HEADER *>(program);

    return ScriptExtCalls(prog, event_no, 15, nullptr, 0) > 0 && ScriptExtCalls(prog, event_no, 6, nullptr, 0) == 0;
}

bool CoopWorldsLinkedTogether() {
    return On() && g_state.up && !g_state.apart && NetplayLocalPlayer() != 0;
}

bool CoopWorldsMutatedDoor() {
    return Mutated("no_partner_door");
}

void CoopWorldsExitBy(int player) {
    // DC_WORLDS_MUTATE=p1_escapes (twin): player 1 leaves whoever chose it, as before.
    if (!On() || !g_state.up || g_state.apart || Mutated("p1_escapes")) {
        return;
    }

    g_town_starter = player;
    std::fprintf(stderr, "worlds: player %d chose the dungeon's way out at link tick %lld\n", player, (long long) g_state.tick);
    std::fflush(stderr);
}

void CoopWorldsSplitForHerEvent() {
    CoopWorldsSplitForEvent(2);
}

bool CoopWorldsSplitForEvent(int player) {
    if (!On() || !g_state.up || g_state.apart || mode != GAME_MODE_EDIT) {
        return true;
    }

    const Zone to = Target(200 + (MapNo == 0 ? 0 : MapNo)); // the town's own dungeon's entrance (Norune's: Divine Beast Cave)

    const bool stays = Player() != player; // (before the split: apart, the link is down)

    g_town_starter = 1;
    SplitTo(to, 200 + to.map, stays, player);
    if (stays) {
        CoopWorldsStayInTown(); // Toan stays where he stands (his copy), or where she stood (hers), whatever is open
    }

    return !stays;
}

// coop-padswap (Jep's 2026-10-27 playtest: after her Cross at the mayor's closet player 2's pad drove Toan for good): the
// game's pad (port 0, Toan's) is hers only while an event she started runs, so she answers its questions and its world
// map. That hand-over is a lease of its own, apart from g_town_starter (who took the town's exit, which the split reads):
// taken when her event starts, given back the tick the event is seen over, and given back too whatever else happens:
// the event never seen running within kHerPadStart ticks (her door's GotoInterior and her menu's TownMapJump run none),
// the pair apart or the link down (a split, a merge: the copies start again from one state), the zone left (the town for
// a room, a room for the town, the town for a floor). Before, the pad stayed hers until the event was seen to end, and
// indoors it was read from the town's mode (still there), so it never ended. DC_WORLDS_MUTATE=room_event_pad_kept (twin):
// the old latch, its event read from the town's mode.
struct HerPad {
    bool         on = false;
    bool         ran = false;
    std::int64_t from = 0;
    Zone         where;
};

HerPad g_her_pad;

constexpr std::int64_t kHerPadStart = 60;

void CoopWorldsTownEventBy(int player) {
    // coop-pt1109a (Jep's 11-09 playtest: "P1 entered the mayor's house and P2 was warped in", tick 740: each copy's split named its own
    // player): her cave door under his Georama or pause splits first (CoopTownHerDoorTakesOver), so this came after the split, on her
    // copy alone, and the 2 outlived it (nothing but a split or her event's end clears it) through her cave and back, over the merge. His
    // next door was then "player 2's" on her copy and his on his: both went in. Apart nobody shares a door: each copy's is its own
    // (CoopWorldsDoorWindowBy). DC_WORLDS_MUTATE=starter_kept_apart (twin): set apart too, kept over the merge, as before.
    if (g_state.apart && !Mutated("starter_kept_apart")) {
        std::fprintf(stderr, "worlds: player %d's event starts apart at link tick %lld: no shared door, the starter stays %d\n", player, (long long) g_state.tick, g_town_starter);
        std::fflush(stderr);
        return;
    }
    g_town_starter = player;
    g_town_starter_ran = false;
    g_her_pad = HerPad{};
    if (player == 2) {
        g_her_pad.on = true;
        g_her_pad.from = g_state.tick;
        g_her_pad.where = Here();
    }
    std::fprintf(stderr, "worlds: player %d starts the town's event at link tick %lld\n", player, (long long) g_state.tick);
    std::fflush(stderr);
}

// The town's event runs: outdoors the town's mode; in a room the room's own (EditInLoop's GameMode, a static of edit_in.cpp:
// the town's GameMode stands still indoors).
static bool TownEventRuns(bool old) {
    // coop-pt1111g (the town's mode on a floor): GameMode is editloop's static, which nothing outside the town sets, and a floor reached by
    // the town's entrance (a town event) finds it at ED_MODE_EVENT: this said a town event ran on the floor (shown: x-door-gate-caveyespad,
    // his floor's line "town event runs 1"). Its callers asked for the town first; the answer is the town's alone now. DC_COOP_MUTATE=
    // townevent_any_mode (twin): any game mode, as before.
    if (mode != GAME_MODE_EDIT && !coop::Mutated("townevent_any_mode")) {
        return false;
    }
    if (EdInteriorFlag != 0) {
        return (old ? GameMode : CoopTownIndoorMode()) == 4; // ED_IN_MODE_EVENT
    }

    return GameMode == ED_MODE_EVENT;
}

static void HerPadBack(const char *why) {
    g_her_pad.on = false;
    std::fprintf(stderr, "worlds: the game's pad back to player 1 at link tick %lld: %s\n", (long long) g_state.tick, why);
    std::fflush(stderr);
}

// The old latch (the twin): hers from her event's start until it was seen running and then not.
static bool P2DrivesLatched() {
    if (g_town_starter != 2 || mode != GAME_MODE_EDIT) {
        return false;
    }

    if (TownEventRuns(true)) {
        g_town_starter_ran = true;
    } else if (g_town_starter_ran) {
        g_town_starter = 1; // over, and it did not leave town
        return false;
    }

    return true;
}

bool CoopWorldsP2Drives() {
    if (Mutated("room_event_pad_kept")) {
        return P2DrivesLatched();
    }

    if (!g_her_pad.on) {
        return false;
    }

    if (!On() || !g_state.up || g_state.apart) {
        HerPadBack("the link went down or apart");
        return false;
    }

    if (mode != GAME_MODE_EDIT || !(Here() == g_her_pad.where)) {
        HerPadBack("the zone changed");
        return false;
    }

    // coop-p2collide: a room's event (her Cross at the mayor's closet) runs in the room's own event mode (EditInLoop's 4).
    if (TownEventRuns(false)) {
        g_her_pad.ran = true;
        return true;
    }

    if (g_her_pad.ran) {
        if (g_town_starter == 2) {
            g_town_starter = 1; // over, and it did not leave town
        }
        HerPadBack("her event is over");
        return false;
    }

    if (g_state.tick - g_her_pad.from > kHerPadStart) {
        HerPadBack("her event never ran");
        return false;
    }

    return true;
}
// Player 2's copy, apart: its player becomes her member chara (a character change, which ends in play). A stand-in (a
// member who has not joined: no weapon of her own) fights with her starting weapon as the partner; this copy is player
// 2's own world now, so she gets it here, as joining the party gives it, and the player can become her (Jep,
// 2026-10-20: his copy stayed Toan). DC_WORLDS_MUTATE=standin_unarmed (twin): she is unarmed here, as before co-op
// gave a stand-in her weapon at the partner load (GrantStandInWeapon), and gets none. Apart now, so this copy alone
// changes. Whether the change runs.
static bool BecomeMember(int chara, const char *why) {
    if (Mutated("standin_unarmed")) {
        UserStatus->equipped_weapon_slot[chara] = -1;
    }

    if (UserStatus->equipped_weapon_slot[chara] < 0 && !Mutated("standin_unarmed")) {
        ((CDngStatusData *) UserStatus)->GetItem(coop::StarterWeapon(chara), 0);
        UserStatus->equipped_weapon_slot[chara] = 0;
        std::fprintf(stderr, "worlds: %s: stand-in %d gets her starting weapon %d\n", why, chara, (int) UserStatus->chara_weapons[chara][0].item_no);
    }

    const bool became = coop::PromptChangePlayer(chara);

    std::fprintf(stderr, "worlds: %s: the player became %d (%d)\n", why, (int) UserStatus->cur_chara, (int) became);
    std::fflush(stderr);
    return became;
}

void CoopWorldsStayOnFloor() {
    char line[200];

    // coop-dungeonp2: player 1's copy staying (player 2 escaped): Toan stays where he is, her partner gone from it.
    if (Player() == 1) {
        Log("stays on the floor as Toan where he stood (player 2 took the way out)");
        return;
    }

    int  chara = coop::g_partner.loaded_chara;
    float pos[4] = {coop::g_partner.pos[0], coop::g_partner.pos[1], coop::g_partner.pos[2], 1.0f};
    bool  placed = coop::g_partner.chara != nullptr && chara >= 0 && chara < 6;

    // (coop-menulocal: a local menu too, which FullMenuOwner, the lockstep menu's, never names; q-p2-quickslot reads the line)
    const int menu = coop::FullMenuOwner() != 0 ? coop::FullMenuOwner() : coop::FullMenuOpenFor(1) ? 1 : coop::FullMenuOpenFor(2) ? 2 : 0;

    coop::FullMenuEndForSplit(); // the leaver's menu, still open here, ends; the pad leaves its stick mode
    std::fprintf(stderr, "worlds: staying: partner chara %d loaded %d, player chara %d, menu open %d\n", chara,
                 (int) (coop::g_partner.chara != nullptr), (int) UserStatus->cur_chara, menu);
    std::fflush(stderr);

    if (placed && chara != UserStatus->cur_chara && !Mutated("no_become")) {
        placed = BecomeMember(chara, "staying");
    }

    if (placed && CharaFrame != nullptr) {
        sceVu0CopyVector(CharaMain.pos, pos);
        CharaFrame->SetPosition(pos);

        // coop-floorexit: her camera goes on as the player's (it was left on the leaver). DC_WORLDS_MUTATE=stay_camera_left (twin):
        // left there, as before.
        if (!Mutated("stay_camera_left")) {
            coop::PlayerCameraFromPartner();
        }
    }

    std::snprintf(line, sizeof line, "stays on the floor as chara %d (player 2's member %d) at (%.0f %.0f %.0f)%s", (int) UserStatus->cur_chara, chara,
                  pos[0], pos[1], pos[2], placed ? "" : "; could not place player 2");
    Log(line);
}

// coop-escapeface (the local-menus thread's find, t-triangle-menu-escape2nd: "player 2's copy draws its player as Toan" for
// the 124 frames of her Escape): linked on a floor the game's player is Toan, player 1's, on both copies, and her member is
// the partner. Her Escape splits the pair at its start (GAME_TASK_ESCAPE_INIT), and apart the partner is drawn on neither
// copy, so her copy played the Escape's effect and fade on Toan. The copy that stays makes its player hers at the split
// (CoopWorldsStayOnFloor); the copy that leaves did not. Now it does too, where she stood and facing as she faced, before
// BtEscape_Init reads the player's place for its effect. The change runs here and not as a task (ChangePlayer's
// GAME_TASK_CHARA_CHANGED and its flash): the Escape's task goes on as it was. An exit's fade (GAME_TASK_EXIT_FADE_WAIT)
// splits at its black end and the map jump follows on that tick, so only the Escape's start has frames to draw.
// DC_WORLDS_MUTATE=leave_as_toan (twin): Toan plays her Escape, as before.
static void LeaveFloorAsMember() {
    if (Player() != 2 || mode != GAME_MODE_DUNGEON || gameTask != GAME_TASK_ESCAPE_INIT || UserStatus == nullptr || CharaFrame == nullptr ||
        Mutated("leave_as_toan")) {
        return;
    }

    const int chara = coop::g_partner.loaded_chara;

    if (coop::g_partner.chara == nullptr || coop::g_partner.chara->frame == nullptr || chara < 0 || chara >= 6 || chara == UserStatus->cur_chara) {
        std::fprintf(stderr, "worlds: leaving: player 2's copy leaves the floor as chara %d (partner %d)\n", (int) UserStatus->cur_chara, chara);
        std::fflush(stderr);
        return;
    }

    float         pos[4] = {coop::g_partner.pos[0], coop::g_partner.pos[1], coop::g_partner.pos[2], 1.0f};
    sceVu0FVECTOR rotation;
    const int     task = gameTask;

    coop::g_partner.chara->frame->GetRotation(rotation);
    if (!BecomeMember(chara, "leaving")) {
        return;
    }
    gameTask = task; // (ChangePlayer's GAME_TASK_CHARA_CHANGED: the Escape goes on instead)
    sceVu0CopyVector(CharaMain.pos, pos);
    CharaFrame->SetPosition(pos);
    CharaMain.SetRotation(rotation[0], rotation[1], rotation[2]);
    CharaFrame->SetRotation(rotation[0], rotation[1], rotation[2]);
    coop::PlayerCameraFromPartner();
    std::fprintf(stderr, "worlds: leaving: player 2's copy plays her member %d out of floor %d at (%.0f %.0f %.0f)\n", chara,
                 (int) UserStatus->cur_floor + 1, pos[0], pos[1], pos[2]);
    std::fflush(stderr);
}

bool CoopWorldsUndoTownEvent() {
    return On() && Player() == 1 && g_last_leaver == 2 && g_state.apart && g_state.stayer && !Mutated("event_not_undone");
}

void CoopWorldsStayInTown() {
    char  line[160];
    float pos[3] = {};

    // coop-doorgate: a door the gate kept shut: nobody moves (the asker walks on where they stand, both copies alike).
    if (doorgate::g_refused) {
        doorgate::g_refused = false;
        return;
    }

    // Player 1's copy staying (player 2 took the exit): Toan is player 1's and stays where he is.
    if (Player() == 1) {
        Log("stays in town as Toan where he stood (player 2 took the exit)");
        return;
    }
    bool  placed = coop::TownPlacePlayerAtPartner(pos);

    std::snprintf(line, sizeof line, "stays in town as Toan at (%.0f %.0f %.0f)%s", pos[0], pos[1], pos[2],
                  placed ? " (where player 2 stood)" : "; player 2 was not in town, Toan stays put");
    Log(line);
}

// The apart copy's own test exit: DC_WORLDS_APART_EXIT_AFTER=K ticks of play after the split, or =heard (coop-fx51c, Jep
// 2026-11-04: no fixed waits): once the other copy's presence says it has heard this one where it is (each knows the other's
// place, what the apart checks read) and this copy's own apart atla (DC_WORLDS_TEST_ATLA), if any, is collected.
bool ApartExitDue() {
    static const char *text = TestEnv("DC_WORLDS_APART_EXIT_AFTER");
    static const bool  on_heard = text != nullptr && std::strcmp(text, "heard") == 0;
    static const int   after = on_heard ? -1 : TestEnvInt("DC_WORLDS_APART_EXIT_AFTER", -1);
    static const int   atla_after = TestEnvInt("DC_WORLDS_TEST_ATLA", -1);

    if (!On() || !g_state.apart) {
        return false;
    }
    if (on_heard) {
        return g_state.peer_heard && g_state.peer_sees_set && g_state.peer_sees == Here() && (atla_after < 0 || g_state.progress_ticks >= atla_after);
    }
    return after >= 0 && ++g_state.apart_ticks >= after;
}

// coop-gatekey52 (Jep, 2026-11-04: no fixed tick waits): DC_WORLDS_EXIT_AT=loaded starts the test's exit once a state file
// (a row's fixture) is put back, the same tick on both linked copies (NetplayStateFileLoaded); =synced once the link's first
// checksum after it is taken too (NetplayStateFileChecked: an exit at once leaves no compare); =key once, besides, the
// floor's gate key is in the party's ring with the game in play (a pickup's presentation over: x-dungeon-p2 keyleave);
// a number, from that link tick.
static const char *TestExitOn() {
    const char *at = TestEnv("DC_WORLDS_EXIT_AT");

    return at != nullptr && (std::strcmp(at, "loaded") == 0 || std::strcmp(at, "synced") == 0 || std::strcmp(at, "key") == 0) ? at : nullptr;
}

static std::int64_t TestExitAt() {
    return TestExitOn() != nullptr ? 0 : TestEnvInt("DC_WORLDS_EXIT_AT", -1);
}

static bool TestExitDue(std::int64_t exit_at) {
    static const char *on = TestExitOn();

    if (on == nullptr) {
        return g_state.tick >= exit_at;
    }
    if (!NetplayStateFileLoaded()) {
        return false;
    }
    if (std::strcmp(on, "synced") == 0) {
        return NetplayStateFileChecked();
    }
    if (std::strcmp(on, "key") != 0) {
        return true;
    }
    if (NowDngMap == nullptr || gameTask != GAME_TASK_PLAY) {
        return false;
    }

    const ITEM_PACK &ring = coop::SharedRing();

    return std::find(std::begin(ring.item), std::end(ring.item), (int) NowDngMap->gate_key_no) != std::end(ring.item);
}

bool CoopWorldsTestTownExit() {
    static const std::int64_t exit_at = TestExitAt();
    static bool               apart_fired = false;

    static bool walking_logged = false;

    if (!On()) {
        return false;
    }

    if (g_state.apart && !walking_logged && CoopDebug()) {
        walking_logged = true;
        std::fprintf(stderr, "worlds: apart and walking in town map %d\n", (int) MapNo);
    }

    // coop-spawn test hook (DC_SPAWN_TEST_STAND=K:X:Y:Z): player 2's copy apart in a town, K ticks of walking after the
    // split, puts her on (X, Y, Z) (where Toan arrives back), and reports where she stands each second after.
    static int stand_ticks = 0;

    if (const char *stand = TestEnv("DC_SPAWN_TEST_STAND"); stand != nullptr && Player() == 2 && g_state.apart && Chara != nullptr &&
                                                           Chara->frame != nullptr && EdInteriorFlag == 0) {
        int   after = -1;
        float to[4] = {0.0f, 0.0f, 0.0f, 1.0f};

        if (std::sscanf(stand, "%d:%f:%f:%f", &after, &to[0], &to[1], &to[2]) == 4 && ++stand_ticks == after) {
            Chara->SetPosition(to);
            Chara->frame->SetPosition(to);
            std::fprintf(stderr, "spawn: test: player 2 stands at (%.0f %.0f %.0f)\n", to[0], to[1], to[2]);
        } else if (stand_ticks > after && stand_ticks % 60 == 0) {
            Chara->GetPosition(to);
            std::fprintf(stderr, "spawn: test: player 2 apart at (%.0f %.0f %.0f)\n", to[0], to[1], to[2]);
        }
    }

    if (exit_at >= 0 && !g_state.exit_fired && g_state.up && TestExitDue(exit_at) && NetplayLocalPlayer() != 0) {
        g_state.exit_fired = true;
        std::fprintf(stderr, "worlds: test: player 1 leaves town through the dungeon gate at link tick %lld\n", (long long) g_state.tick);
        return true;
    }

    // DC_WORLDS_TEST_EXIT_AFTER_JOIN=K (both copies): K link ticks after the first tick of a catch-up join's lockstep (a
    // merge's), player 1's way out through the gate again, once: the host splits while a slow joiner may still be
    // replaying (coop-leaverace).
    static const int after_join = TestEnvInt("DC_WORLDS_TEST_EXIT_AFTER_JOIN", -1);
    static long long join_from = -1;
    static bool      join_fired = false;
    static bool      join_logged = false;

    if (after_join >= 0 && !join_logged && g_state.up && !g_state.apart) {
        join_logged = true;
        std::fprintf(stderr, "worlds: test: together in town at link tick %lld (linked by a catch-up %d)\n", (long long) g_state.tick,
                     NetplayLinkedByCatchup() ? 1 : 0);
    }

    if (after_join >= 0 && !join_fired && g_state.up && !g_state.apart && NetplayLinkedByCatchup() && NetplayLocalPlayer() != 0) {
        if (join_from < 0) {
            join_from = g_state.tick;
        }
        if (g_state.tick - join_from >= after_join) {
            join_fired = true;
            std::fprintf(stderr, "worlds: test: player 1 leaves town through the dungeon gate at link tick %lld, %d after the catch-up join\n",
                         (long long) g_state.tick, after_join);
            return true;
        }
    }

    // With DC_WORLDS_TEST_GATE_AGAIN (split, merge, split again) the apart copy back in town waits for the merge instead.
    // DC_WORLDS_TEST_WAIT_MERGE=1: the same, without the walk-on armed again.
    static const bool again = TestEnvInt("DC_WORLDS_TEST_GATE_AGAIN", 0) != 0 || TestEnvInt("DC_WORLDS_TEST_WAIT_MERGE", 0) != 0;

    // coop-flagsync: not while the other copy is in this same town: a merge comes once this copy's arrival (its visit,
    // the town's own flags, now sent to the other copy) is acknowledged, a round trip after its first walking tick (before,
    // nothing waited and the merge came first; w-world-split -Case merge-town left again, OUT OF SYNC).
    const bool merging = g_state.peer_heard && Here() == g_state.peer;

    if (!apart_fired && !again && !merging && ApartExitDue()) {
        apart_fired = true;
        std::fprintf(stderr, "worlds: test: this copy (apart) leaves town through the dungeon gate\n");
        return true;
    }

    return false;
}

namespace {

// coop-floorgate (Jep, 2026-10-28, "gather first"): a floor whose story is
// still to come is gone into by both players or not at all. Its gate asks the other "Join?" (the door vote, with no leave
// anyway: a no keeps both where they are, the asker told so and still playing); apart, the gate refuses and the other copy is
// called. game.coop_story_gather false (the host's, as every setting the link shares): the gates as before.
enum Gather {
    kGatherNone = 0,
    kGatherScene = 1, // its opening scene is still unseen (the game flag it sets, NetplayFloorSceneFlag)
    kGatherBoss = 2,  // its boss is still alive
    kGatherItem = 3,  // pt1111c: an item used at a spot starts a story scene that needs every player (CoopWorldsItemSceneAsk)
};

// The dungeons' names, as the town's world map gives them.
const char *const kDungeonNames[7] = {"Divine Beast Cave", "Wise Owl Forest", "Shipwreck", "Sun and Moon Temple", "Moon Sea", "Gallery of Time",
                                      "Demon Shaft"};

// Whether the boss of dungeon `map` (its last floor) is beaten, by what the boss's win leaves in the save (each dungeon's
// event.stb, program 510, the boss win): the story counter (game int flag 0) at or
// past the value the win sets, or a game flag the win sets. Gallery of Time's floor resets its own flags on every visit, so
// its boss counts as alive until Demon Shaft's counter (700, inferred: the counter only grows in the scripts read).
bool BossBeaten(int map) {
    struct Beaten {
        bool counter; // int flag 0 >= value; else game flag `value` set
        int  value;
    };
    static constexpr Beaten kBeaten[7] = {{true, 100}, {false, 34}, {true, 300}, {true, 400}, {false, 56}, {true, 700}, {true, 700}};

    if (map < 0 || map >= 7 || SaveData == nullptr || Mutated("boss_flag_ignored")) {
        return false;
    }

    const Beaten &beaten = kBeaten[map];

    return beaten.counter ? SaveData->GetGameIntFlag(0) >= beaten.value : SaveData->GetGameFlag(beaten.value) != 0;
}

// Whether floor `to` (0 up) of dungeon `map` is one both players go into, and why (a Gather).
int GatherFor(int map, int to) {
    if (!CoopWorldsStoryGather() || Mutated("floor_gate_off") || map < 0 || map >= 7 || to < 0 || SaveData == nullptr) {
        return kGatherNone;
    }

    const int flag = NetplayFloorStoryFlag(map, to); // coop-b4story: a mid-floor story too (the cave's floor 4)

    if (flag >= 0 && SaveData->GetGameFlag(flag) == 0) {
        return kGatherScene;
    }

    // The boss's floor: the dungeon's last, as the game's own table has it (its entrance window marks it with the boss's name).
    if (to == maxFloorTbl__3[map] - 1 && !Mutated("boss_floor_off") && !BossBeaten(map)) {
        return kGatherBoss;
    }

    return kGatherNone;
}

// "Divine Beast Cave floor 2"
const char *FloorName(int map, int to) {
    static char name[64];

    std::snprintf(name, sizeof name, "%s floor %d", map >= 0 && map < 7 ? kDungeonNames[map] : "the dungeon", to + 1);
    return name;
}

// The note the asker of a gate gets when it does not open (the other said no, or is not here), on the asker's copy for 4 s.
struct GatherNote {
    int                                   player = 0; // 1 or 2: whose screen
    char                                  text[160] = "";
    std::chrono::steady_clock::time_point at{};
};

GatherNote g_gather_note;

// notice: an answer (coop-gatepopup: doorgate::AnswerNotice, the join notice's style), else the link line's note.
void SetGatherNote(int player, const char *text, bool notice = false) {
    std::fprintf(stderr, "worlds: gather: note for player %d: \"%s\"\n", player, text);
    std::fflush(stderr);
    if (notice && doorgate::AnswerNotice(player, text)) {
        g_gather_note.player = 0;
        return;
    }
    g_gather_note.player = player;
    std::snprintf(g_gather_note.text, sizeof g_gather_note.text, "%s", text);
    g_gather_note.at = std::chrono::steady_clock::now();
}

// coop-floorgate (Jep, 2026-10-28, option b: "sync them to start the floor together"; "they should never see Floor 4 ... sync
// from like frame one of floor 5"): the call, apart. The caller's gate stays shut (GatherRefuseApart) and the other player is
// asked in the game's window, once free (playing, no menu, no event). A yes: the caller's gate opens for the caller (its
// window answered "the next floor"), and the other's screen fades to black where it is; once the caller's copy is on the new
// floor, the other's goes straight onto it from wherever it was (NetplayGoToFloor, as a floor-start checkpoint's copy enters it)
// and joins there as any arrival does (first on the floor sends; held behind black while the caller's opening scene plays,
// CoopWorldsHoldFloorStart). A no tells the caller. Each call is asked once.
struct CallAsk {
    int  from = 0;  // the caller (1 or 2); 0 none
    int  map = -1;  // the dungeon, the floor called to and the caller's floor (0 up)
    int  to = -1;
    int  floor = -1;
    int  cursor = 0;   // the hand: 0 come, 1 not now
    int  shown = 0;    // frames shown
    std::uint16_t held = 0;
    bool waited = false; // (coop-playfix: its wait for this copy's floor menu logged)
};

CallAsk g_call_ask;

// coop-gateflow: a call's floor (to) that is the go-along question of a gate taken to the floor menu (AskAlong, below).
constexpr int kAlongTo = -3;
void          ReachNextFloor();

// The caller's copy: the gate whose call was answered "coming", to take now (CallTick), and let through once (CoopWorldsHoldFloorChange).
struct CallGo {
    int map = -1;
    int floor = -1;  // the caller's floor (0 up)
    int script = -1; // its gate's script
};

CallGo g_call_go;
int    g_call_script = -1; // the caller's gate refused last (its script), for the yes to keep
int    g_pick_pending = -1; // coop-gatepopup2: the floor this copy's refused floor-menu pick called for (0 up), -1 none
int    g_pick_go = -1;      // (its call answered coming: picked at the next step, CoopWorldsEntrancePad)

// The called copy after its yes: the floor it follows the caller onto (0 up), once the caller's copy is there, its screen black
// meanwhile; given up (back from black) after 60 s.
struct CallFollow {
    int                                   map = -1;
    int                                   to = -1;
    std::chrono::steady_clock::time_point since{};
};

CallFollow g_call_follow;

void CallReceived(int from, int map, int to, int floor) {
    g_call_ask = CallAsk{from, map, to, floor};
    std::fprintf(stderr, "worlds: gather: player %d calls this copy (player %d) to %s, from floor %d\n", from, Player(), FloorName(map, to), floor + 1);
    std::fflush(stderr);
}

// coop-gatepopup: the caller withdrew its call (its Circle on the waiting box): the question closes on this copy, its player told.
void CallWithdrawn(int from) {
    char line[96];

    if (g_call_ask.from != from) {
        return;
    }
    g_call_ask = CallAsk{};
    std::snprintf(line, sizeof line, "Player %d cancelled the invite", from);
    SetGatherNote(Player(), line, true);
    std::fprintf(stderr, "worlds: gather: player %d withdrew the call: its question closes on this copy\n", from);
    std::fflush(stderr);
}

void CallAnswered(int map, int to, int answer) {
    const bool coming = answer == 1;
    char       line[160];

    if (doorgate::g_wait_answer != std::chrono::steady_clock::time_point{}) {
        doorgate::WaitForAnswer(false); // coop-gatepopup: the asker's wait ends
    }
    if (answer == 3) { // coop-gatepopup: its question timed out on the other copy
        std::snprintf(line, sizeof line, "Player %d didn't answer. %s needs both players", 3 - Player(), FloorName(map, to));
    } else if (answer == 2) {
        std::snprintf(line, sizeof line, "Player %d is busy right now. %s needs both players", 3 - Player(), FloorName(map, to));
    } else if (coming && !Mutated("call_yes_ignored") && g_pick_pending == to) {
        g_pick_go = to; // coop-gatepopup2: the floor menu's pick goes through (CoopWorldsEntrancePad)
        std::snprintf(line, sizeof line, "Player %d is coming. On to %s", 3 - Player(), FloorName(map, to));
    } else if (coming && !Mutated("call_yes_ignored")) {
        g_call_go = CallGo{map, to - 1, g_call_script};
        std::snprintf(line, sizeof line, "Player %d is coming. On to %s", 3 - Player(), FloorName(map, to));
    } else {
        std::snprintf(line, sizeof line, "Player %d can't come now. %s needs both players", 3 - Player(), FloorName(map, to));
    }

    g_pick_pending = -1;
    SetGatherNote(Player(), line, true);
    std::fprintf(stderr, "worlds: gather: the call to %s answered: %s (gate script %d kept %d)\n", FloorName(map, to),
                 coming ? "coming" : answer == 2 ? "busy" : answer == 3 ? "no answer" : "not now", g_call_script, (int) (g_call_go.map >= 0));
    std::fflush(stderr);
}

// Whether this copy's player is free to be asked: playing a floor or walking a town, no menu, no event.
bool CallFree() {
    // coop-playfix (Jep's 2026-10-31 Local playtest: she picked B1 at the floor menu while he was still coming into the cave,
    // was told he was busy, picked again and it went): at a dungeon's entrance its floor menu idle on its pick is free (the
    // call drawn over it, DrawEntranceOverlays; the menu takes none of its keys meanwhile, CoopWorldsEntrancePad); its way in
    // before the menu (the splash, script 401) waits in CallTick. DC_WORLDS_MUTATE=call_entrance_busy (twin): busy, as before.
    if (mode == GAME_MODE_DUNGEON && BtGameModeFlag == BT_GAME_MODE_ENTRANCE_MENU && !Mutated("call_entrance_busy")) {
        return DEnterMenu.state == DUN_ENTER_SELECT && !coop::FullMenuOpenFor(Player());
    }
    if (mode == GAME_MODE_DUNGEON) {
        return gameTask == GAME_TASK_PLAY && BtGameModeFlag == BT_GAME_MODE_PLAY && BtEventMode == 0 && !coop::FullMenuOpenFor(Player());
    }

    // coop-gatepopup2 (Jep's 2026-10-28 playtest: the cave's call was drawn over P2's talk with the mayor, his name tag over its
    // first line): a talk's message window is up while the room (or the town) still reads walk. DC_WORLDS_MUTATE=talk_not_busy
    // (twin): a talk does not count, as before.
    if (mode == GAME_MODE_EDIT && EdSystemMesCheck() != 0 && !Mutated("talk_not_busy")) {
        return false;
    }

    // Inside a building the town's GameMode is not ED_MODE_WALK while its player walks (the room's own mode is: the door gate
    // thread saw 12 as P1 walked the mayor's house), so a building asks its own walk. DC_WORLDS_MUTATE=call_indoor_busy (twin):
    // the town's walk asked indoors too, as first built (anyone in a building read busy).
    if (EdInteriorFlag != 0 && !Mutated("call_indoor_busy")) {
        return mode == GAME_MODE_EDIT && !coop::FullMenuOpenFor(Player()) && CoopTownIndoorWalks();
    }

    return mode == GAME_MODE_EDIT && GameMode == ED_MODE_WALK && !coop::FullMenuOpenFor(Player()) &&
           (EdInteriorFlag == 0 || CoopTownIndoorWalks());
}

// Apart, each frame: the call's question, once this player is free; its keys from this copy's own pad. DC_WORLDS_TEST_CALL=yes|no
// (the test's): answered so a second after it shows.
void CallTick() {
    CallAsk &c = g_call_ask;

    // coop-gatepopup: on a floor, this copy waiting for its call's answer: its own Circle withdraws it (the town's is stepped
    // with the door's question, CoopWorldsDoorQuestionPad).
    if (g_state.apart && mode == GAME_MODE_DUNGEON && doorgate::Waiting()) {
        doorgate::WaitCircle(CoopWorldsCallPadButtons()); // (coop-gatepopup3)
    }

    // The caller's copy, its call answered "coming": its gate's script now, on the floor it called from, its escape window
    // answered "the next floor" (CoopWorldsHoldFloorChange lets this one through).
    if (g_call_go.map >= 0 && g_state.apart && mode == GAME_MODE_DUNGEON && UserStatus != nullptr && gameTask == GAME_TASK_PLAY &&
        BtGameModeFlag == BT_GAME_MODE_PLAY) {
        const CallGo go = g_call_go;
        CDungeonEventData *gate = go.map == selectMapNo && go.floor == UserStatus->cur_floor ? GateEvent(go.script) : nullptr;

        if (gate == nullptr || gate->event == nullptr) {
            g_call_go = CallGo{};
            std::fprintf(stderr, "worlds: gather: the called gate (map %d floor %d script %d) is not here: dropped\n", go.map, go.floor + 1, go.script);
            std::fflush(stderr);
        } else {
            BtEventInfo.script_no = gate->event->script_no;
            BtEventInfo.ext_memory = gate->event->ext_mem;
            BtEventInfo.event_marker = true;
            BtEventInfo.action_mode = 1;
            sceVu0CopyVector(BtEventInfo.position, gate->pos);
            sceVu0CopyVector(BtEventInfo.direction, gate->dir);
            coop::SetEscapeWindowAnswer(1);
            gameTask = GAME_TASK_SCRIPT_START;
            std::fprintf(stderr, "worlds: gather: the call answered coming: this copy (player %d) takes its gate on floor %d to floor %d\n", Player(),
                         go.floor + 1, go.floor + 2);
            std::fflush(stderr);
        }
    }

    // DC_WORLDS_TEST_CALL_INDOORS=1 (the test's, w-indoor -Case callindoors): this copy, apart and walking inside a building for
    // 30 ticks, once gets a call as from the other player (to the cave's floor 2 from its floor 1), as a floor gate's would.
    static const bool test_indoors = TestEnvInt("DC_WORLDS_TEST_CALL_INDOORS", 0) != 0;
    static int        indoor_walk = 0;
    static bool       indoor_called = false;

    if (test_indoors && !indoor_called && c.from == 0 && g_state.apart && mode == GAME_MODE_EDIT && EdInteriorFlag != 0 && CoopTownIndoorWalks() &&
        ++indoor_walk >= 30) {
        indoor_called = true;
        std::fprintf(stderr, "worlds: test: a call injected on this copy (player %d) walking inside %s\n", Player(), EdInteriorName);
        CallReceived(3 - Player(), 0, 1, 0);
    }

    // The called copy after its yes: black where it is until the caller's copy is on the floor, then straight onto it.
    if (g_call_follow.map >= 0) {
        const CallFollow f = g_call_follow;
        // coop-joinatla (Jep's 2026-10-28 Steam session: her yes to his B1 pick, then 20 s and more of black and back in town):
        // a floor whose opening scene plays first reports the way onto it (-2) till the scene is over (B1's lesson, a minute
        // and more), so "on the floor" never came within the wait. His way onto this floor, its build's random state heard
        // (PeerOpensFloor's test), counts: she goes onto it and joins his scene there (coop-b1lesson). DC_WORLDS_MUTATE=
        // call_scene_wait (twin): the floor itself only, as before.
        const bool opening = g_state.peer_heard && g_state.peer.mode == GAME_MODE_DUNGEON && g_state.peer.map == f.map && g_state.peer.floor == -2 &&
                             g_state.peer_floor_rand_set && g_state.peer_floor_rand_map == f.map && g_state.peer_floor_rand_floor == f.to &&
                             !Mutated("call_scene_wait");
        const bool       there = g_state.peer_heard && g_state.peer.mode == GAME_MODE_DUNGEON && g_state.peer.map == f.map &&
                           (g_state.peer.floor == f.to || opening);

        if (there || !g_state.apart) {
            g_call_follow = CallFollow{};

            if (there && g_state.apart) {
                std::fprintf(stderr, "worlds: gather: player %d is on %s%s: this copy follows straight onto it\n", 3 - Player(), FloorName(f.map, f.to),
                             opening ? " (its opening scene playing)" : "");
                std::fflush(stderr);
                NetplayGoToFloor(f.map, f.to);
            }
        } else if (std::chrono::steady_clock::now() - f.since > std::chrono::seconds(60)) {
            g_call_follow = CallFollow{};
            EdFadeIn(30, 0.0f, 0.0f, 0.0f);
            SetGatherNote(Player(), "The other player did not go on. You stay here", true);
            std::fprintf(stderr, "worlds: gather: player %d never reached %s in 60 s: this copy stays\n", 3 - Player(), FloorName(f.map, f.to));
            std::fflush(stderr);
        }
    }

    if (c.from == 0) {
        return;
    }

    if (!g_state.apart) {
        c = CallAsk{};
        return;
    }

    // (Jep, 2026-10-28: "guards in place to prevent that popup from disrupting something like a cutscene or minigame ... it should
    // just autoreject in the case with the message that the other player is busy"): a player not free (a scene, an event, a
    // talk, a minigame, a menu) is not asked; the caller is told this player is busy. Once asked, a question that loses its
    // player to something else (a monster's hit starts no event; a scene would) is answered busy too. DC_WORLDS_TEST_CALL_BUSY=1
    // (the test's): this copy counts as busy. DC_WORLDS_MUTATE=call_busy_waits (twin): it waits for the player, as first built.
    static const bool test_busy = TestEnvInt("DC_WORLDS_TEST_CALL_BUSY", 0) != 0;

    // coop-gateflow: the go-along question is this copy's own: it waits while its player is busy (the split's own change of
    // player runs first) and goes once this copy has left that floor.
    if (c.to == kAlongTo && (mode != GAME_MODE_DUNGEON || UserStatus == nullptr || selectMapNo != c.map || UserStatus->cur_floor != c.floor)) {
        c = CallAsk{};
        std::fprintf(stderr, "worlds: gateflow: this copy (player %d) left the floor: the go-along question closes\n", Player());
        std::fflush(stderr);
        return;
    }
    if (c.to == kAlongTo && !CallFree() && !test_busy) {
        c.shown = 0;
        return;
    }

    if (!CallFree() || test_busy) {
        // coop-playfix: coming into a dungeon's entrance (its splash before the floor menu) is no scene of its own: the call
        // waits for the menu (CallFree) and is asked there.
        const bool way_in = mode == GAME_MODE_DUNGEON && BtGameModeFlag == BT_GAME_MODE_ENTRANCE_MENU && !Mutated("call_entrance_busy");

        if (Mutated("call_busy_waits") || (way_in && !test_busy)) {
            if (way_in && !c.waited) {
                c.waited = true;
                std::fprintf(stderr, "worlds: gather: player %d's call waits for this copy's floor menu (player %d coming into the entrance)\n", c.from, Player());
                std::fflush(stderr);
            }
            c.shown = 0;
            return;
        }

        const CallAsk asked = c;

        c = CallAsk{};
        if (asked.to == kAlongTo) { // coop-gateflow: this copy's own question: no answer goes back
            std::fprintf(stderr, "worlds: gateflow: this copy (player %d) is busy: the go-along question closes\n", Player());
            std::fflush(stderr);
            return;
        }
        QueueProgress(kWhatCallAnswer, asked.to, 2, asked.map, 0);
        std::fprintf(stderr, "worlds: gather: this copy (player %d) is busy (mode %d task %d game mode %d event %d): player %d's call is not asked\n",
                     Player(), (int) mode, (int) gameTask, (int) BtGameModeFlag, (int) BtEventMode, asked.from);
        std::fflush(stderr);
        return;
    }

    const std::uint16_t buttons = CoopWorldsCallPadButtons(); // (coop-gatepopup3: the game's pad as it reads it)
    // (coop-gatepopup: nothing in the first kAnswerGrace frames)
    const std::uint16_t pressed = c.shown == 0 || doorgate::InGrace(c.shown) ? 0 : buttons & ~c.held;
    static const char  *test = TestEnv("DC_WORLDS_TEST_CALL");

    c.held = buttons;

    if (c.shown++ == 0) {
        std::fprintf(stderr, "worlds: gather: the call is asked on this copy (player %d)\n", Player());
        std::fflush(stderr);
    }

    // coop-gatepopup: unanswered for kAskTicks frames, the caller is told so (3), as the door's call (its asker waits meanwhile).
    if (c.shown > doorgate::kAskTicks && test == nullptr) {
        const CallAsk asked = c;

        c = CallAsk{};
        if (asked.to == kAlongTo) { // coop-gateflow
            std::fprintf(stderr, "worlds: gateflow: the go-along question on this copy (player %d) unanswered: it stays\n", Player());
            std::fflush(stderr);
            return;
        }
        QueueProgress(kWhatCallAnswer, asked.to, 3, asked.map, 0);
        std::fprintf(stderr, "worlds: gather: this copy (player %d) leaves player %d's call unanswered\n", Player(), asked.from);
        std::fflush(stderr);
        return;
    }

    bool yes = false, no = false;

    if (test != nullptr) {
        yes = c.shown == 60 && std::strncmp(test, "yes", 3) == 0;
        no = c.shown == 60 && std::strncmp(test, "no", 2) == 0;
    } else {
        coop::SoundOwner sound(Player());

        if ((pressed & (kInputUp | kInputDown)) != 0) {
            c.cursor = 1 - c.cursor;
            SndSePlay(MENU_SOUND_CURSOR, -1, 0);
        }

        yes = (pressed & kInputCross) != 0 && c.cursor == 0;
        no = (pressed & kInputCircle) != 0 || ((pressed & kInputCross) != 0 && c.cursor == 1);

        if (yes || no) {
            SndSePlay(yes ? MENU_SOUND_CONFIRM : MENU_SOUND_REFUSE, -1, 0);
        }
    }

    if (!yes && !no) {
        return;
    }

    const CallAsk asked = c;

    c = CallAsk{};

    // coop-gateflow: the go-along question: its yes takes this copy to its own floor menu; nothing goes back.
    if (asked.to == kAlongTo) {
        std::fprintf(stderr, "worlds: gateflow: this copy (player %d) %s player %d to the floor menu\n", Player(), yes ? "goes along with" : "stays: not going with",
                     asked.from);
        std::fflush(stderr);
        if (yes && !Mutated("along_stays")) {
            if (UserStatus != nullptr) {
                ReachNextFloor();
            }
            NetplayGoToFloor(asked.map, -1);
        }
        return;
    }

    QueueProgress(kWhatCallAnswer, asked.to, yes ? 1 : 0, asked.map, 0);
    std::fprintf(stderr, "worlds: gather: this copy (player %d) answers player %d's call: %s\n", Player(), asked.from, yes ? "coming" : "not now");
    std::fflush(stderr);

    // DC_WORLDS_MUTATE=call_stays (twin): says yes but stays.
    if (yes && !Mutated("call_stays")) {
        // DC_WORLDS_MUTATE=call_via_floor (twin): onto the caller's floor at once (it then sees that floor, and the caller's gate),
        // as first built.
        if (Mutated("call_via_floor")) {
            NetplayGoToFloor(asked.map, asked.floor);
        } else {
            g_call_follow = CallFollow{asked.map, asked.to, std::chrono::steady_clock::now()};
            EdFadeOut(30, 0.0f, 0.0f, 0.0f);
            std::fprintf(stderr, "worlds: gather: this copy's screen fades to black; it follows player %d onto %s once that copy is there\n", asked.from,
                         FloorName(asked.map, asked.to));
            std::fflush(stderr);
        }
    }
}

// coop-gatepopup2 (Jep's 2026-10-28 playtest after train 17: "The prompt to enter B1 happened when Toan pressed square on the
// town dungeon door, but before toan had used the key on the door. The prompt should probably happen when toan selects B1 since
// that's how he would have to interact with the other levels"): apart at a dungeon's entrance floor menu, the pick (Cross) of a
// floor that needs both players (GatherFor: its opening scene unseen, its boss alive) does not take: the menu stays, the player
// is told, the other copy is called as by a floor's gate (kWhatCall, floor -1: from the menu). Their yes: the pick goes through
// (g_pick_go: the menu's hand put on it and Cross given) and the other copy follows onto that floor (CallTick's follow). Not
// while the other copy waits at this same menu (the merge there brings both). DC_WORLDS_MUTATE=pick_gate_off (twin): no gate.
void PickRefuseApart(int to) {
    char line[200];

    std::snprintf(line, sizeof line, "%s needs both players. Player %d has been asked to come", FloorName(selectMapNo, to), 3 - Player());
    SetGatherNote(Player(), line);
    std::snprintf(line, sizeof line, "apart: the floor menu's pick of %s does not take (it needs both players); player %d stays at the menu",
                  FloorName(selectMapNo, to), Player());
    Log(line);
    g_pick_pending = to;
    g_call_script = -1;
    QueueProgress(kWhatCall, to, 0, selectMapNo, -1);
    doorgate::WaitForAnswer(true);
    doorgate::g_sent = doorgate::SentCall{kWhatCall, to, 0, selectMapNo, -1};
}

// coop-floorgate: an apart copy's gate to a floor that needs both players: it stays shut, the player is told, and the other
// copy gets a call (once, then again only after 10 s, a gate taken again and again calls once).
void GatherRefuseApart() {
    static std::chrono::steady_clock::time_point called{};
    static int                                   called_for = -1;
    const auto                                   now = std::chrono::steady_clock::now();
    const int                                    to = UserStatus->cur_floor + 1;
    char                                         line[200];

    gameTask = GAME_TASK_PLAY;
    std::snprintf(line, sizeof line, "%s needs both players. Player %d has been asked to come", FloorName(selectMapNo, to), 3 - Player());
    SetGatherNote(Player(), line);
    std::snprintf(line, sizeof line, "apart: the gate to %s does not open (it needs both players); player %d stays on floor %d", FloorName(selectMapNo, to),
                  Player(), to);
    Log(line);

    if (called_for != selectMapNo * 256 + to || now - called > std::chrono::seconds(10)) {
        called_for = selectMapNo * 256 + to;
        called = now;
        g_call_script = BtEventInfo.script_no;
        QueueProgress(kWhatCall, to, 0, selectMapNo, to - 1);
        doorgate::WaitForAnswer(true); // coop-gatepopup
        doorgate::g_sent = doorgate::SentCall{kWhatCall, to, 0, selectMapNo, to - 1};
    }
}

} // namespace

bool CoopWorldsPlayerFree() {
    return CallFree();
}

bool CoopWorldsStoryGather() {
    // DC_WORLDS_MUTATE=gather_config_ignored (twin): gathered whatever game.coop_story_gather says.
    return dc::platform::ConfigGet().coop_story_gather || Mutated("gather_config_ignored");
}

bool CoopWorldsHoldFloorChange(int script_no) {
    // How long a finished floor waits for the merge before going on alone.
    constexpr int kFloorHoldMs = 20000;

    if (On() && !g_state.apart && g_state.floor_hold && g_state.merge_pending && mode == GAME_MODE_DUNGEON) {
        return true; // merging: held until the reload leaves the floor
    }

    if (!On() || !g_state.apart || mode != GAME_MODE_DUNGEON || UserStatus == nullptr) {
        g_state.floor_hold = false;
        return false;
    }

    // Only the floor's exit gate (its event named "out"), and once a floor: a hold given up lets the gate go.
    bool gate = false;

    if (NowEventMan != nullptr && script_no >= 0) {
        for (const CDungeonEventData &data : NowEventMan->event) {
            if (data.event != nullptr && data.enabled && data.event->script_no == script_no && std::strncmp(data.event->name, "out", 3) == 0) {
                gate = true;
            }
        }
    }

    if (!gate) {
        return false;
    }

    auto now = std::chrono::steady_clock::now();
    bool waits = g_state.peer_heard && g_state.peer.mode == GAME_MODE_DUNGEON && g_state.peer.map == selectMapNo && g_state.peer.floor == -1 &&
                 !Mutated("no_floor_merge");
    // coop-floorgate: apart, a next floor that needs both players is not gone into alone: the gate does not start (the task
    // is play again: never held), this player is told, and the other copy is called. Only while the other copy is heard from
    // (a player gone from the link holds nobody back), and not while it waits at this dungeon's entrance (the hold below,
    // whose merge brings both onto this floor, where the gate asks as it does together).
    int gather = g_state.peer_heard ? GatherFor(selectMapNo, UserStatus->cur_floor + 1) : kGatherNone;

    // The gate of a call answered "coming" goes through once (the other copy follows onto the floor it leads to).
    if (g_call_go.map == selectMapNo && g_call_go.floor == UserStatus->cur_floor && g_call_go.script == script_no) {
        g_call_go = CallGo{};
        gather = kGatherNone;
        Log("apart: the called gate opens (the other player said yes and follows)");
        return false;
    }

    if (gather != kGatherNone && (!waits || (g_state.floor_hold && now - g_state.floor_hold_since > std::chrono::milliseconds(kFloorHoldMs)))) {
        GatherRefuseApart();
        g_state.floor_hold = false;
        return true;
    }

    if (g_state.hold_given_up_map == selectMapNo && g_state.hold_given_up_floor == UserStatus->cur_floor) {
        return false;
    }

    if (!g_state.floor_hold) {
        if (!waits) {
            return false;
        }

        char where[96];
        char line[200];

        g_state.floor_hold = true;
        g_state.floor_hold_since = now;
        g_state.presence_due = now; // tell the other copy at once
        Describe(g_state.peer, where, sizeof where);
        std::snprintf(line, sizeof line, "holding the floor change: player %d waits %s; both go in together from there", 3 - Player(), where);
        Log(line);
        return true;
    }

    if (!waits || now - g_state.floor_hold_since > std::chrono::milliseconds(kFloorHoldMs)) {
        g_state.floor_hold = false;
        g_state.hold_given_up_map = selectMapNo;
        g_state.hold_given_up_floor = UserStatus->cur_floor;
        g_state.presence_due = now;
        Log(waits ? "no merge at the entrance in time; the next floor alone" : "the other player left the entrance; the next floor alone");
        return false;
    }

    return true;
}

void CoopWorldsStateTaken(bool own) {
    // coop-floorscene (x-floor-join -Case reverse -Seamless red 1 in 3 on 8d7f149: player 1 came onto B1 while her lesson still
    // played): a checkpoint's self-take (player 1's copy takes its own state as a mode ends apart, Checkpoint) is not the other's
    // state. It marked his copy "the other's state in" for the rest of the split, so his floor start was never held for her
    // opening scene (CoopWorldsHoldFloorStart), both copies took themselves for there first and the merge went by his state,
    // out of her lesson; and his gains apart were rebased away each tick (statsync Tick). DC_WORLDS_MUTATE=own_take_merged
    // (twin): marked, as before.
    g_merge_state_in = g_state.apart && (!own || Mutated("own_take_merged")); // coop-localfix
    // coop-mergebags: DC_WORLDS_MUTATE=join_drops_key (w-town-apart -Case bagssolo's twin): the host's state taken without
    // player 1's cave key, as Jep saw it go (2026-10-25).
    if (Mutated("join_drops_key") && slotsync::Party() != nullptr) {
        ITEM_PACK &pack = slotsync::Party()->item_pack;

        for (int i = 0; i < 103; i++) {
            if (pack.item[i] == ITEM_CAVE_KEY) {
                pack.item[i] = -1;
                pack.item_vol[i] = 0;
            }
        }
    }

    slotsync::BagDump("state taken"); // coop-mergebags
    if (g_state.floor_hold && !g_state.apart) {
        Log("the floor change held for the merge ends with the host's state");
    }

    if (!g_state.apart) {
        g_state.floor_hold = false;
    }
}

// coop-saveload: test hook (DC_WORLDS_TEST_APART_PRESS=BUTTON:T,...): player 1's copy apart in a town holds BUTTON
// (triangle, cross, circle, up, down) on its own first pad for 4 ticks from T ticks of town play after the split, as
// DC_COOP_MENUPRESS does on the agreed pads while linked (apart there are none). Logs each press.
// coop-bagapart: DC_WORLDS_TEST_APART_PRESS_P2 (same form) on player 2's copy apart. Her copy keeps its input script
// (her title Join), which sets the first pad every frame, so her presses go into the game's pad read instead
// (CoopWorldsTestPadButtons, from MenuFilterPort).
static std::uint16_t g_test_p2_buttons = 0;

static void TestMergeInMenu() {
    static const char *spec = TestEnv("DC_WORLDS_TEST_MERGE_IN_MENU");
    static int         walk_ticks = 0, wait_ticks = -1;

    if (spec == nullptr || Player() != 1) {
        return;
    }

    InputPadState pad{};

    pad.connected = true;
    pad.left_x = pad.left_y = pad.right_x = pad.right_y = 0x80;
    if (g_state.apart && !NetplayCatchUnderWay() && mode == GAME_MODE_EDIT && EdInteriorFlag == 0 && GameMode == ED_MODE_WALK) {
        if (walk_ticks % 8 < 4) {
            pad.buttons |= TestEnv("DC_WORLDS_TEST_MERGE_IN_MENU_BUILD") != nullptr ? kInputSelect : kInputTriangle;
        }
        walk_ticks++;
    } else if (NetplayCatchWaitsToWalk()) {
        const int after = ++wait_ticks - std::atoi(spec);

        if (after >= 0 && after % 60 < 4) {
            pad.buttons |= kInputCircle;
            if (after % 60 == 0) {
                std::fprintf(stderr, "worlds: test: player 1 presses Circle %d ticks into the wait to walk (town mode %d)\n", wait_ticks, (int) GameMode);
            }
        }
    }
    InputSetOverride(0, &pad);
}

// coop-stage4 4c test hook (DC_WORLDS_TEST_APART_DOOR=X:Z:Y:YAW, x-town-stayermenu -Indoors): player 2's copy apart, walking in the town, is
// put once at a door (its own Cross then takes it), after 120 ticks of its walk there (the room's menu up on the other copy by then).
static void TestApartDoor() {
    static const char *spec = TestEnv("DC_WORLDS_TEST_APART_DOOR");
    static const int   who = TestEnvInt("DC_WORLDS_TEST_APART_DOOR_PLAYER", 2); // (coop-stage4 4e part 2: =1 his copy, she stays in the room)
    static int         walks = 0;
    float              x = 0.0f, z = 0.0f, y = 0.0f, yaw = 0.0f;

    if (spec == nullptr || walks > 120 || Player() != who || !g_state.apart || mode != GAME_MODE_EDIT || EdInteriorFlag != 0 || GameMode != ED_MODE_WALK || Chara == nullptr ||
        Chara->frame == nullptr || ++walks <= 120 || std::sscanf(spec, "%f:%f:%f:%f", &x, &z, &y, &yaw) < 4) {
        return;
    }
    float at[4] = {x, y, z, 1.0f};
    float rotation[4] = {0.0f, yaw, 0.0f, 0.0f};

    Chara->SetPosition(at);
    Chara->SetRotation(rotation);
    std::fprintf(stderr, "worlds: test: player 2's copy apart is put at the door (%.0f %.0f)\n", x, z);
}

// coop-pt1111e test hook (DC_QUEST_TEST_STAGE=P:QUEST:STAGE, x-quest-apart): on player P's copy apart, at its 30th apart walk tick, QUEST
// moved to STAGE (its progress through SaveJson/LoadJson, as a quest's own SetStage would leave it); each copy then says the quest's
// stage as the merge lands (QuestStageAtMerge).
static std::string TestQuestStage(const char *quest) {
    const nlohmann::json saved = nlohmann::json::parse((dc::quest::QuestHostSetup(), dc::quest::SaveJson()), nullptr, false);

    if (saved.is_discarded() || !saved.contains("quests") || !saved["quests"].contains(quest) || !saved["quests"][quest].contains("stage")) {
        return "(none)";
    }
    return saved["quests"][quest]["stage"].get<std::string>();
}

static void TestQuestApart() {
    static const char *const spec = TestEnv("DC_QUEST_TEST_STAGE");
    static int               ticks = 0;
    static bool              done = false;
    int                      who = 0;
    char                     quest[64] = {}, stage[64] = {};

    // (DC_QUEST_TEST_STAGE_LINKED=1, x-quest-apart -Parted: linked and together instead, on player P's copy alone: the copies part, the
    // checksum's "quests" part says so, and the resync carries the host's progress back)
    static const bool linked = TestEnvOn("DC_QUEST_TEST_STAGE_LINKED");
    const bool        when = linked ? !g_state.apart && NetplayLinkUp() && NetplayStateFileLoaded() : g_state.apart && !NetplayCatchUnderWay();

    if (spec == nullptr || done || !when || std::sscanf(spec, "%d:%63[^:]:%63s", &who, quest, stage) != 3 || Player() != who || ++ticks < 30) {
        return;
    }
    done = true;
    nlohmann::json saved = nlohmann::json::parse((dc::quest::QuestHostSetup(), dc::quest::SaveJson()), nullptr, false);

    if (saved.is_discarded() || !saved.contains("quests")) {
        return;
    }
    saved["quests"][quest]["stage"] = stage;
    dc::quest::LoadJson(saved.dump());
    std::fprintf(stderr, "worlds: test: quest %s to stage %s on player %d's copy apart (now %s)\n", quest, stage, who, TestQuestStage(quest).c_str());
}

std::string CoopWorldsQuestsMoved() {
    nlohmann::json saved = nlohmann::json::parse((dc::quest::QuestHostSetup(), dc::quest::SaveJson()), nullptr, false);
    nlohmann::json moved = nlohmann::json::object();

    if (saved.is_discarded() || !saved.contains("quests") || !saved["quests"].is_object()) {
        return "{}";
    }
    for (const auto &[id, progress] : saved["quests"].items()) {
        const dc::quest::Definition *quest = dc::quest::Find(id);
        const bool fresh = quest != nullptr && progress.is_object() && progress.value("stage", std::string()) == quest->FirstStage() &&
                           (!progress.contains("vars") || progress["vars"].empty());

        if (!fresh) {
            moved[id] = progress;
        }
    }
    return moved.dump();
}

// coop-pt1111e test hook (DC_QUEST_TEST_STAGE_AT_TAKE=QUEST:STAGE, x-quest-apart -AtTake): on the copy sending its state set, QUEST moved to
// STAGE as the set is taken, inside the merge's catch-up (no apart record goes for it then: only the set carries it to the taker).
void CoopWorldsTestQuestAtTake() {
    static const char *const spec = TestEnv("DC_QUEST_TEST_STAGE_AT_TAKE");
    static bool              done = false;
    char                     quest[64] = {}, stage[64] = {};

    if (spec == nullptr || done || std::sscanf(spec, "%63[^:]:%63s", quest, stage) != 2) {
        return;
    }
    done = true;
    nlohmann::json saved = nlohmann::json::parse((dc::quest::QuestHostSetup(), dc::quest::SaveJson()), nullptr, false);

    if (saved.is_discarded() || !saved.contains("quests")) {
        return;
    }
    saved["quests"][quest]["stage"] = stage;
    dc::quest::LoadJson(saved.dump());
    std::fprintf(stderr, "worlds: test: quest %s to stage %s on player %d's copy as its state set is taken (now %s)\n", quest, stage, Player(),
                 TestQuestStage(quest).c_str());
}

static void QuestStageAtMerge() {
    static const char *const spec = TestEnv("DC_QUEST_TEST_STAGE");
    int                      who = 0;
    char                     quest[64] = {}, stage[64] = {};

    if (spec != nullptr && std::sscanf(spec, "%d:%63[^:]:%63s", &who, quest, stage) == 3) {
        std::fprintf(stderr, "worlds: test: quest %s at stage %s on this copy as the merge lands (player %d)\n", quest, TestQuestStage(quest).c_str(), Player());
    }
    static const char *const at_take = TestEnv("DC_QUEST_TEST_STAGE_AT_TAKE");

    if (at_take != nullptr && std::sscanf(at_take, "%63[^:]:%63s", quest, stage) == 2) {
        std::fprintf(stderr, "worlds: test: quest %s at stage %s on this copy as the merge lands (player %d)\n", quest, TestQuestStage(quest).c_str(), Player());
    }
}

static void TestApartPresses() {
    TestQuestApart(); // coop-pt1111e
    TestMergeInMenu();
    TestApartDoor();

    static const char *p2_merge_menu = TestEnv("DC_WORLDS_TEST_MERGE_IN_MENU_P2");

    if (p2_merge_menu != nullptr && Player() == 2) {
        static int walk_ticks = 0;
        const bool walks = g_state.apart && !NetplayCatchUnderWay() && mode == GAME_MODE_EDIT && EdInteriorFlag == 0 && GameMode == ED_MODE_WALK;

        // =talk (coop-stage4 4e part 2): her Cross every 8 ticks apart, with DC_COOP_TEST_TOWN_TALK_APART putting her before a villager.
        if (std::strcmp(p2_merge_menu, "talk") == 0) {
            g_test_p2_buttons = walks && !NetplayLinkUp() && walk_ticks++ % 8 < 4 ? kInputCross : 0;
            return;
        }
        // =shop (coop-stage4 r6): shop 1 as its keeper's talk opens it, once she walks apart outdoors, her menu for the merge.
        if (std::strcmp(p2_merge_menu, "shop") == 0) {
            static bool opened = false;

            if (walks && !NetplayLinkUp() && !opened) {
                opened = true;
                EdSetShopNo(1);
                goto_menu = ED_MENU_SHOP;
                std::fprintf(stderr, "worlds: test: her shop opens apart\n");
            }
            g_test_p2_buttons = 0;
            return;
        }
        g_test_p2_buttons = walks && !NetplayLinkUp() && walk_ticks++ % 8 < 4 ? (std::strncmp(p2_merge_menu, "build", 5) == 0 ? kInputSelect : kInputTriangle) : 0;
        if (std::strcmp(p2_merge_menu, "buildpart") == 0 && g_state.apart && !NetplayLinkUp()) {
            CoopGeoramaTestPickApart(); // (coop-stage4 r4: a part in her hand as the merge comes)
        }
        return;
    }

    static const char *spec1 = TestEnv("DC_WORLDS_TEST_APART_PRESS");
    static const char *spec2 = TestEnv("DC_WORLDS_TEST_APART_PRESS_P2");
    static int         ticks = -1;
    const bool         hers = spec2 != nullptr && Player() == 2;
    const char        *spec = hers ? spec2 : spec1;

    if (spec == nullptr) {
        return;
    }

    // The copy runs with no input script (its script would take the pad back each frame): the first pad is this hook's
    // throughout, at rest but for the presses, so no live device reaches the test copy.
    InputPadState pad{};

    pad.connected = true;
    pad.left_x = pad.left_y = pad.right_x = pad.right_y = 0x80;
    // (coop-twinfix: none once this copy has started the merge: its presses reach this copy's game alone, never the link's
    // pads the other's catch-up replays, so one in the merge's own ticks parted the copies: w-town-apart -ApartMenuOpen, her
    // Circle at apart tick 3160 after "merging in Norune", OUT OF SYNC 9 ticks after it, 3 runs of 3 under load)
    if (g_state.apart && g_merging_split == g_state.split_tick) {
        g_test_p2_buttons = 0;
        if (!hers) {
            InputSetOverride(0, &pad);
        }
        return;
    }
    if ((Player() != 1 && !hers) || !g_state.apart || mode != GAME_MODE_EDIT) {
        ticks = -1;
        g_test_p2_buttons = 0;
        if (!hers) {
            InputSetOverride(0, &pad);
        }
        return;
    }
    ticks++;
    for (const char *at = spec; at != nullptr && *at != '\0';) {
        char name[16] = {};
        int  when = -1;

        if (std::sscanf(at, "%15[a-z]:%d", name, &when) == 2 && ticks >= when && ticks < when + 4) {
            pad.buttons |= std::strcmp(name, "triangle") == 0 ? kInputTriangle
                           : std::strcmp(name, "cross") == 0  ? kInputCross
                           : std::strcmp(name, "circle") == 0 ? kInputCircle
                           : std::strcmp(name, "up") == 0     ? kInputUp
                           : std::strcmp(name, "down") == 0   ? kInputDown
                           : std::strcmp(name, "select") == 0 ? kInputSelect // coop-stable1: his Georama apart (w-town-apart -P1Busy georama -P2Out)
                           : std::strcmp(name, "square") == 0 ? kInputSquare // coop-doorfix: the cave door's picker apart (x-cave-door apart)
                                                              : 0;
            if (ticks == when) {
                std::fprintf(stderr, "worlds: test: player %d presses %s at apart tick %d\n", hers ? 2 : 1, name, ticks);
            }
        }
        // coop-bagapart: "shot:T" writes this copy's frame to DC_WORLDS_TEST_APART_SHOTS\apart-T.png (to look at).
        const char *dir = TestEnv("DC_WORLDS_TEST_APART_SHOTS");

        if (dir != nullptr && std::strcmp(name, "shot") == 0 && ticks == when) {
            static char path[512];
            std::snprintf(path, sizeof path, "%s/apart-%d.png", dir, when);
            NetplayTestShotTo(path);
        }
        at = std::strchr(at, ',');
        at = at != nullptr ? at + 1 : nullptr;
    }
    if (hers) {
        g_test_p2_buttons = (std::uint16_t) pad.buttons;
        return;
    }
    InputSetOverride(0, &pad);
}

std::uint16_t CoopWorldsTestPadButtons() {
    static const bool p2_merge_menu = TestEnv("DC_WORLDS_TEST_MERGE_IN_MENU_P2") != nullptr;

    return p2_merge_menu && (!g_state.apart || NetplayLinkUp()) ? 0 : g_test_p2_buttons; // (coop-stage4 4c: none left over past its link)
}

// coop-startjoin: test hook (DC_WORLDS_TEST_PICK_ON_PEER=1, x-floor-join -Case reverse -StartJoin): player 1's copy, apart at
// a dungeon's floor menu, picks floor 1 (Cross) as soon as it hears player 2 leave that menu for it, so it arrives while her
// floor's arrival still runs (Jep's 2026-10-27 run: "P2 starting the level start animation for B1 and P1 joining immediately
// after"). Then Cross every 40 ticks, as the test's scripts step a scene's messages. The copy runs with no input script:
// the first pad is this hook's throughout.
// coop-floorscene: DC_WORLDS_TEST_PICK_ON_PEER=2 picks once it hears her floor started (0: her opening scene over), so
// x-floor-join -Case reverse arrives after her B1 lesson on every run (a fixed frame raced it: 1 run in 3 came during it).
static void TestPickOnPeer() {
    static const int  on = TestEnvInt("DC_WORLDS_TEST_PICK_ON_PEER", 0);
    static int        ticks = -1;
    static bool       logged = false;

    if (!on) {
        return;
    }

    InputPadState pad{};

    pad.connected = true;
    pad.left_x = pad.left_y = pad.right_x = pad.right_y = 0x80;
    // (she sets out from the floor menu: heard there, -1, then on the way, -2, or on the floor, 0)
    static bool menu_heard = false;
    const int   peer = mode == GAME_MODE_DUNGEON && selectMapNo >= 0 ? CoopWorldsPeerFloorIn(selectMapNo) : -1000;

    menu_heard = menu_heard || peer == -1;
    if (Player() == 1 && ticks < 0 && g_state.apart && BtGameModeFlag == BT_GAME_MODE_ENTRANCE_MENU && menu_heard && (peer == 0 || (peer == -2 && on != 2)) && on != 3) {
        ticks = 0;
    }
    // coop-floorstay: DC_WORLDS_TEST_PICK_ON_PEER=3 (x-floor-stay, player 1's copy with no input script: the hook has the pad
    // throughout). Apart: at the cave's floor menu Cross every 30 (floor 1, where the cursor starts: his first pick, and his
    // second once he hears her on it), elsewhere in the dungeon Cross every 40 (his opening scene's messages), out of it the
    // world map's presses (up twice, Cross, every 40: onto the cave, as x-floor-join's B). Together on a floor, Cross every 40
    // (both copies' pads agree there: lockstep); together in the town, nothing (the split's own hook moves him).
    if (on == 3) {
        static int  frame = 0;
        static bool second_logged = false;
        static int  town_frames = 0;
        const bool  menu = BtGameModeFlag == BT_GAME_MODE_ENTRANCE_MENU && mode == GAME_MODE_DUNGEON;
        const int   f = frame++;

        if (Player() != 1) {
            return;
        }
        if (g_state.apart && menu) {
            if (peer == 0 && !second_logged) {
                second_logged = true;
                std::fprintf(stderr, "worlds: test: player 1 picks floor 1 again as it hears player 2 on it\n");
            }
            pad.buttons |= f % 30 < 4 ? kInputCross : 0;
        } else if (mode == GAME_MODE_DUNGEON) {
            pad.buttons |= f % 40 < 4 ? kInputCross : 0;
        } else if (g_state.apart && (mode != GAME_MODE_EDIT || ++town_frames > 650)) { // (in the town once the gate hook, at 600, has put him on its entrance)
            const int s = f % 40;

            pad.buttons |= s < 4 || (s >= 10 && s < 14) ? kInputUp : s >= 30 && s < 34 ? kInputCross : 0;
        }
        InputSetOverride(0, &pad);
        return;
    }
    if (ticks >= 0) {
        if (!logged && BtGameModeFlag == BT_GAME_MODE_ENTRANCE_MENU) {
            logged = true;
            std::fprintf(stderr, "worlds: test: player 1 picks floor 1 as it hears player 2 %s (%s)\n", on == 2 ? "start it" : "set out for it",
                         peer == 0 ? "on it" : "on the way");
        }
        // coop-joinbudget: DC_WORLDS_TEST_PICK_MENU_ONLY=1, Cross at the floor menu alone, none after the pick (x-floor-join
        // -BoneDoor: his Cross in the picker took the key before her Square, now that its pick is this hook's, not a fixed frame's)
        static const bool menu_only = TestEnvOn("DC_WORLDS_TEST_PICK_MENU_ONLY");

        if (ticks % (BtGameModeFlag == BT_GAME_MODE_ENTRANCE_MENU ? 30 : 40) < 4 && (!menu_only || BtGameModeFlag == BT_GAME_MODE_ENTRANCE_MENU)) {
            pad.buttons |= kInputCross;
        }
        ticks++;
    }
    InputSetOverride(0, &pad);
}

// coop-saveload: player 1's load from his menu while apart, to her copy over the progress channel (sent until acknowledged).
// DC_COOP_MUTATE=saveload_noapart (twin): nothing sent.
void CoopWorldsLoaded(int file_no) {
    if (Player() == 1 && g_state.apart) {
        statsync::Loaded(); // coop-apartstats: the save's stats are no gains
    }
    if (Player() != 1 || coop::Mutated("saveload_noapart")) {
        return;
    }
    QueueProgress(kWhatLoaded, file_no, 0, 0, 0);
    // coop-loadpull: the next merge is by his loaded state, wherever she is (MergeReady); his recording from before the load
    // is not where a catch-up may start from (its replay would load her copy's own save 0).
    if (g_state.apart && !Mutated("load_no_pull")) {
        g_state.load_pull = true;
        NetplayLoadPull();
        Log("player 1's load: the next merge is by his loaded state, wherever player 2 is");
    }
}

bool CoopWorldsLoadPull() {
    return On() && g_state.apart && g_state.load_pull && !Mutated("load_no_pull");
}

std::int64_t CoopWorldsTicksSinceMerge() {
    return g_state.up && !g_state.apart && g_state.merge_tick >= 0 ? g_state.tick - g_state.merge_tick : -1;
}

// coop-floorscene2 (Jep's 2026-10-30 playtest, item 4: B3's opening scene never played for player 1): flags this copy sets for
// itself alone (netplay's MarkFloorScenesSeen: her copy at the entrance marks the scenes of his floor seen so her own build
// goes straight to play and the merge takes his state) are not story progress for the other copy. They went to his copy as
// progress, landed before his floor 3 started, and its first-visit scene (game flag 106) was skipped. While on, the flag
// setter sends nothing. DC_WORLDS_MUTATE=local_flags_sent (twin): sent, as before.
void CoopWorldsLocalFlags(bool on) {
    g_flags_local = on && !Mutated("local_flags_sent");
}

// coop-pt1109e: apart, and this copy has heard the other copy in the zone it stands in.
bool CoopWorldsPeerHeardHere() {
    return g_state.apart && g_state.peer == Here();
}

bool CoopWorldsPeerHeard() {
    return g_state.apart && g_state.peer_heard;
}

bool CoopWorldsApart() {
    return On() && g_state.apart;
}

// coop-dunentry (Jep's 2026-10-30 playtest, item 14): how long ago player 2 left this copy's map (her door, or her walk into a
// dungeon), by this copy's clock, on player 1's copy staying behind; -1 otherwise. Draw-only use (coop_town keeps her drawn
// through her way out's fade on his screen).
long long CoopWorldsHerLeaveMs() {
    if (!On() || !g_state.apart || !g_state.stayer || g_last_leaver != 2 || Player() != 1) {
        return -1;
    }

    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - g_state.split_at).count();
}

bool CoopWorldsSplitPeerAlive() {
    if (!On() || !g_state.apart || g_state.dropped) {
        return false;
    }

    const auto now = std::chrono::steady_clock::now();

    return now - g_state.split_at < std::chrono::seconds(10) || (g_state.peer_heard && now - g_state.peer_at < std::chrono::seconds(10));
}

bool CoopWorldsPeerStand(float *pos, float *yaw) {
    // (coop-houseenter: in the room this copy is in too, its stand marked indoor)
    const int stand_map = EdInteriorFlag != 0 && !Mutated("stayer_outdoors") ? (MapNo | coop::kIndoorStand) : MapNo;

    if (!g_state.peer_stand.set || g_state.peer_stand.map != stand_map || (EdInteriorFlag != 0 && !(g_state.peer == Here()))) {
        return false;
    }
    for (int i = 0; i < 3; i++) {
        pos[i] = g_state.peer_stand.pos[i];
    }
    *yaw = g_state.peer_stand.yaw;
    return true;
}

bool CoopWorldsPreJoinBuilds() {
    return On() && Player() == 2 && !g_state.apart && !NetplayLinkUp() && mode == GAME_MODE_EDIT && ApartBuilds();
}

bool CoopWorldsOwnStand(float *pos, float *yaw, std::int32_t *map) {
    if (mode != GAME_MODE_EDIT || EdInteriorFlag != 0 || GameMode != ED_MODE_WALK || Chara == nullptr || Chara->frame == nullptr) {
        return false;
    }

    float at[4];
    float rotation[4];

    Chara->GetPosition(at);
    Chara->GetRotation(rotation);
    for (int i = 0; i < 3; i++) {
        pos[i] = at[i];
    }
    *yaw = rotation[1];
    *map = MapNo;
    return true;
}

bool CoopWorldsArrivedWhereStayer(int within_ms) {
    // When this copy came into the place it draws, asked each frame drawn (the town's fade-in runs frames with no tick: the
    // town showed dim for a few of them before the hold, l-local-play -HostSeries): a town's outside from its first frame.
    if (const Zone here = {mode, mode == GAME_MODE_EDIT ? (int) MapNo : Here().map, mode == GAME_MODE_EDIT && EdInteriorFlag == 0 ? -1 : Here().floor};
        !(here == g_arrival.zone)) {
        g_arrival.zone = here;
        g_arrival.at = std::chrono::steady_clock::now();
        if (CoopDebug()) {
            std::fprintf(stderr, "worlds: arrival: now drawing mode %d map %d place %d (interior flag %d, edit mode %d)\n", here.mode, here.map, here.floor,
                         (int) EdInteriorFlag, (int) GameMode);
        }
    }

    const Zone &here = g_arrival.zone;

    // coop-pt1111g (the town's mode on a floor; DC_COOP_DEBUG): the town's GameMode is editloop's static, which nothing outside the town sets,
    // so a floor reached by the town's entrance (a town event) finds it at ED_MODE_EVENT. 60 frames into each floor, what every reader of the
    // town's event mode answers there (each must say no: the town's alone); x-floor-townmode's line. DC_COOP_MUTATE=scene_any_mode (pt1111b's
    // twin) makes the first say yes.
    static Zone floor_seen = {};
    static int  floor_frames = 0;

    if (!(floor_seen == here)) {
        floor_seen = here;
        floor_frames = 0;
    }
    if (CoopDebug() && here.mode == GAME_MODE_DUNGEON && ++floor_frames == 60) { // (reads only: nothing here changes a thing)
        std::fprintf(stderr, "worlds: on floor %d the town's mode is %d; its event readers say: scene watched together %d, own town event %d, scene here %d, "
                             "busy here %d, her event carries %d, town event runs %d, walks through an own event %d/%d\n",
                     here.floor, (int) GameMode, CoopTownSceneWatchedTogether() ? 1 : 0, CoopTownOwnTownEvent(), SceneHere() ? 1 : 0, BusyHere() ? 1 : 0,
                     CoopWorldsHerEventCarries() ? 1 : 0, TownEventRuns(false) ? 1 : 0, CoopTownWalksThroughOwnEvent(1) ? 1 : 0, CoopTownWalksThroughOwnEvent(2) ? 1 : 0);
    }

    // (coop-houseenter: inside a building too)
    // (coop-mayorhouse: not while the other is in a talk indoors: no merge comes till it ends, she plays the room. coop-scenejoin:
    // a scene of his holds her black here as anywhere, since the merge goes into it)
    return On() && g_state.apart && !FirstHere() && here.mode == GAME_MODE_EDIT && (here.floor < 0 || !Mutated("stayer_outdoors")) && g_state.peer_heard &&
           g_state.peer == here && !g_state.peer_busy && !g_state.peer_builds && std::chrono::steady_clock::now() - g_arrival.at < std::chrono::milliseconds(within_ms);
}

bool CoopWorldsPeerHere() {
    return On() && g_state.apart && g_state.peer_heard && g_state.peer == Here();
}

bool CoopWorldsFloorFirst();

bool CoopWorldsSendsMerge() {
    // (and her copy waiting to join, walking in the town before his copy has arrived: the join goes by her state too)
    // coop-dungeonjoin: on a floor, the copy that was on it first (the other arrived where it plays).
    // (coop-loadpull: never after player 1's load: his state is the merge's)
    return On() && ((g_state.apart && FirstHere() && mode == GAME_MODE_EDIT) || CoopTownPreJoinHere(nullptr, nullptr) || CoopWorldsFloorFirst()) &&
           !CoopWorldsLoadPull() && !NetplayMutated("host_always_sends");
}

bool CoopWorldsPartnerSpeaks() {
    if (!On()) {
        return false;
    }

    // Her own world: everything said on her copy is said to her.
    if (g_state.apart && Player() == 2) {
        return true;
    }

    // coop-stage4 4e part 2: her event carried through her merge, on every copy alike.
    if (CoopTownSwapDeferred()) {
        return true;
    }

    // coop-roomevent: together, her own room event (the mayor's closet), on both copies alike.
    if (CoopTownHerRoomEventOn() && mode == GAME_MODE_EDIT && EdInteriorFlag != 0) {
        return true;
    }

    // coop-pt1109b (Jep's 2026-11-09 playtest: a villager indoors called P2 by her character's name while she was alone, "Toan" once
    // P1 came in): her talk with a villager, indoors or out, together, is said to her too. CoopTownHerTalkOn is not CoopTownHerRoomEventOn
    // (her talk is beside the room's script), so the rule above never covered it and the talk text read the game's player, Toan; alone,
    // she is the game's player on her copy and the name was right there. DC_COOP_MUTATE=hertalk_name_his (twin): not covered, as before.
    if (CoopTownHerTalkOn() && mode == GAME_MODE_EDIT && !CoopMutateHas("hertalk_name_his")) { // (this file's Mutated reads DC_WORLDS_MUTATE; the rows' twins set DC_COOP_MUTATE)
        return true;
    }

    // Together, the town event she started (CoopWorldsP2Drives' rule, read only), on both copies alike.
    if (Mutated("room_event_pad_kept")) {
        return g_town_starter == 2 && mode == GAME_MODE_EDIT && TownEventRuns(true);
    }

    return g_her_pad.on && g_state.up && !g_state.apart && mode == GAME_MODE_EDIT && TownEventRuns(false); // coop-p2collide: a room's too
}

bool CoopWorldsBagsSwapped() {
    return slotsync::g_bags_swapped;
}

void CoopWorldsBagDump(const char *label) {
    slotsync::BagDump(label);
}

namespace {

// The door vote (together on a floor): one player starts the floor's exit gate, the other is asked whether to
// leave too. Yes: both go, as before. No: the one who started it is asked "leave anyway?"; yes splits the pair
// (that player goes on alone, the other stays on the floor), no keeps both where they are. Simulation state,
// answered from the shared pads on the same ticks in both copies.
struct Vote {
    bool          active = false;
    int           asker = 0;      // 0 player 1, 1 player 2: who started the gate
    int           stage = 0;      // 1 the other is asked, 2 the asker is asked to leave anyway
    int           script_no = -1; // the gate's script, held while the vote runs
    int           ticks = 0;      // left to answer (10 s)
    std::uint16_t held = 0;       // buttons already down when the question came
    int           passed = -1;    // a gate script let through once (both leave, or the leaver goes)
    bool          beside = false; // coop-p2prompt: asked beside the running game (her gate prompt), no script held
    int           gather = 0;     // coop-floorgate: the next floor needs both players (a Gather), asked as "Join?" with no leave anyway
    int           to = -1;        // (and that floor, 0 up)
    int           cursor = 0;     // coop-floorgate: the gathered question's hand: 0 go together, 1 not yet
    std::uint16_t asker_held = 0xFFFF; // coop-gatepopup: the asker's pad, for its Circle (cancel)
    int           wait_cursor = 1;     // coop-gatevote: the asker's box's hand: 0 LEAVE NOW, 1 CANCEL (where it starts: a Cross still
                                       // held or mashed from the gate's own Cross cancels, never leaves alone)
};

Vote g_vote;

// coop-gatevote (Jep, 2026-11-02: "both of them have question prompts on the screen (which should use the in game UI if they
// don't already)"): every gate question together is asked in the game's own window with its hand (Up/Down, Cross; Circle no),
// as the gathered floor's was, and the asker waits in the game's box (its Circle withdraws). A gathered floor has no leave
// anyway. DC_WORLDS_MUTATE=vote_hud_text (twin): the plain questions on the link line, Cross yes / Circle no, as before.
bool VoteKeysInWindow() {
    return g_vote.gather != kGatherNone ? g_vote.stage == 1 : !Mutated("vote_hud_text");
}

bool VoteDrawnInWindow() {
    return VoteKeysInWindow() && !(g_vote.gather != kGatherNone && Mutated("gather_hud_text"));
}

// coop-p2prompt: the gate a vote asked beside the game is for (its script starts when both say yes).
struct BesideGate {
    int           ext = 0;
    int           answer = 1; // the asker's choice in the escape window: 1 the next floor, 2 back to town
    sceVu0FVECTOR position{};
    sceVu0FVECTOR direction{};
};

BesideGate g_beside_gate;

// The gate to start again on player 2's copy once its change of member has run (leave anyway by player 2).
struct GateAgain {
    int           script = -1;
    int           ext = 0;
    sceVu0FVECTOR position{};
    sceVu0FVECTOR direction{};
};

GateAgain g_gate_again;

constexpr int kVoteTicks = 600;

InputPadState VotePad(int port) {
    InputPadState linked;

    return NetplayPad(port, linked) ? linked : InputGetPad(port);
}

// The floor's exit gate (its event named "out") whose script this is.
CDungeonEventData *GateEvent(int script_no) {
    if (NowEventMan == nullptr || script_no < 0) {
        return nullptr;
    }

    for (CDungeonEventData &data : NowEventMan->event) {
        if (data.event != nullptr && data.enabled && data.event->script_no == script_no && std::strncmp(data.event->name, "out", 3) == 0) {
            return &data;
        }
    }

    return nullptr;
}

float Dist2(const float *a, const float *b) {
    float dx = a[0] - b[0];
    float dz = a[2] - b[2];

    return dx * dx + dz * dz;
}

void VoteLog(const char *line) {
    std::fprintf(stderr, "worlds: vote: %s\n", line);
    std::fflush(stderr);
}

// coop-floorgate: the gathered gate's no: both stay where they are, the asker told so (never held: the task is play again).
void GatherDeclined(bool timed_out) {
    char line[200];

    std::snprintf(line, sizeof line, "%s: %s needs both players; both stay", timed_out ? "no answer" : "the other player says no",
                  FloorName(selectMapNo, g_vote.to));
    VoteLog(line);
    std::snprintf(line, sizeof line, "Player %d isn't coming yet. %s needs both players", 2 - g_vote.asker, FloorName(selectMapNo, g_vote.to));
    SetGatherNote(g_vote.asker + 1, line, true);
}

// coop-gatepopup (Jep: the asker's waiting box, Circle cancels the invite): the gathered question's asker's agreed Circle, past
// the grace, on both copies alike. True: withdrawn (the one asked told; the caller clears the vote). DC_WORLDS_MUTATE=call_cancel_off.
// coop-gatevote (Jep, 2026-11-02: "the asker should be considered paused while they have the waiting prompt which should have an
// option to cancel and leave without the players that haven't accepted yet, along with a ready status for every player"): the
// asker's box (coop::DrawWaitingBox) is stepped here, from its agreed pad on both copies alike: Up and Down move its hand, Cross
// takes LEAVE NOW (2: it goes alone, the pair splits) or CANCEL (1), Circle cancels. A gathered floor has no LEAVE NOW (it needs
// both players). A test's scripted vote (DC_WORLDS_TEST_VOTE) keeps the Circle alone. Withdrawn: both stay, the other told.
int AskerPick() {
    const std::uint16_t mine = VotePad(g_vote.asker).buttons;
    const std::uint16_t pressed = doorgate::InGrace(kVoteTicks - g_vote.ticks) ? 0 : mine & ~g_vote.asker_held;
    const bool          scripted = TestEnv("DC_WORLDS_TEST_VOTE") != nullptr;
    const bool          can_leave = g_vote.gather == kGatherNone;
    const bool          own = NetplayLocalPlayer() == g_vote.asker + 1;
    char                line[96];
    int                 pick = 0;

    g_vote.asker_held = mine;
    if (!VoteKeysInWindow()) {
        return 0;
    }
    if (!scripted && can_leave && (pressed & (kInputUp | kInputDown)) != 0) {
        g_vote.wait_cursor = 1 - g_vote.wait_cursor;
        if (own) {
            coop::SoundOwner sound(g_vote.asker + 1);

            SndSePlay(MENU_SOUND_CURSOR, -1, 0);
        }
    }
    if ((pressed & kInputCircle) != 0) {
        pick = 1;
    } else if (!scripted && (pressed & kInputCross) != 0) {
        pick = can_leave && g_vote.wait_cursor == 0 ? 2 : 1;
    }
    if (pick == 0 || (pick == 1 && Mutated("call_cancel_off"))) {
        return 0;
    }
    if (own) {
        coop::SoundOwner sound(g_vote.asker + 1);

        SndSePlay(pick == 2 ? MENU_SOUND_CONFIRM : MENU_SOUND_REFUSE, -1, 0);
    }
    if (pick == 2) {
        std::snprintf(line, sizeof line, "player %d leaves now, without the others (its box's LEAVE NOW)", g_vote.asker + 1);
        VoteLog(line);
        return 2;
    }
    std::snprintf(line, sizeof line, "player %d withdraws the question (Circle): both stay", g_vote.asker + 1);
    VoteLog(line);
    std::snprintf(line, sizeof line, "Player %d cancelled the invite", g_vote.asker + 1);
    SetGatherNote(2 - g_vote.asker, line, true);
    return 1;
}

// coop-floorgate (Jep, 2026-10-28: the game's own window with the hand at yes or no): the gathered question's keys, from the
// asked player's agreed pad on both copies alike: Up and Down move the hand, Cross takes the row it points at, Circle is a no.
void GatherKeys(std::uint16_t pressed, bool *yes, bool *no) {
    const int asked = g_vote.stage == 1 ? 1 - g_vote.asker : g_vote.asker;

    if ((pressed & (kInputUp | kInputDown)) != 0) {
        coop::SoundOwner sound(asked + 1);

        g_vote.cursor = 1 - g_vote.cursor;
        SndSePlay(MENU_SOUND_CURSOR, -1, 0);
    }

    *yes = (pressed & kInputCross) != 0 && g_vote.cursor == 0;
    *no = (pressed & kInputCircle) != 0 || ((pressed & kInputCross) != 0 && g_vote.cursor == 1);

    if (*yes || *no) {
        coop::SoundOwner sound(asked + 1);

        SndSePlay(*yes ? MENU_SOUND_CONFIRM : MENU_SOUND_REFUSE, -1, 0);
    }
}

// A test's answer for the one asked (DC_WORLDS_TEST_VOTE: "yes" or "no" for the other player, then ",yes" or
// ",no" for the asker's leave anyway), given a second after the question; else none.
int TestAnswer() {
    static const char *const answers = TestEnv("DC_WORLDS_TEST_VOTE");

    if (answers == nullptr || g_vote.ticks != kVoteTicks - 60) {
        return 0;
    }

    const char *at = answers;

    if (g_vote.stage == 2) {
        at = std::strchr(answers, ',');
        at = at != nullptr ? at + 1 : "";
    }

    return std::strncmp(at, "yes", 3) == 0 ? 1 : std::strncmp(at, "no", 2) == 0 ? -1 : 0;
}

void AskAlong(int leaver); // coop-gateflow (below)

// Leave anyway: the asker's copy (asker 0 player 1, 1 player 2) goes on through the gate, the other's stays on this
// floor. 1: this copy goes through now; 0: it stays; 2: it goes once player 2's change of member has run.
// coop-gateflow: to_picker, the gate's player leaves for the dungeon's floor menu instead (its copy: 3, the task play; the
// restart takes it there on the next frame), and the one staying is asked to go along (AskAlong).
int FloorSplit(int asker, bool to_picker = false) {
    int  leaver = asker + 1;
    bool goes = NetplayLocalPlayer() == leaver;
    char line[200];

    g_state.apart = true;
    FollowApart(); // coop-dungeonp2
    g_state.stayer = !goes;
    g_state.split_tick = g_state.tick;
    g_state.split_at = std::chrono::steady_clock::now(); // coop-natsplit
    g_state.dropped = false;
    // coop-floorexit (x-floor-exit -Map 0: he left anyway within 15 s of their floor merge): the link going down at this split
    // is no merge that failed to hold, which put back the stayer from before the merge (she, who stays, was "the one that
    // left") and held her copy on black as an arriver for seconds. DC_WORLDS_MUTATE=split_unheld (twin): as before.
    if (!Mutated("split_unheld")) {
        MergeHeld(); // coop-pt1111j: his load's pull ends with it too (the hold's own end did that; this split skipped it)
    }
    g_state.peer = goes ? Here() : Zone{GAME_MODE_DUNGEON, selectMapNo, to_picker ? -1 : UserStatus->cur_floor + 1};
    g_state.peer_heard = false;
    g_state.peer_busy = false; // coop-mayorhouse
    g_state.peer_talk = false;
    g_state.peer_builds = false; // coop-buildarrive
    g_state.peer_sees_set = false;
    g_state.presence_due = std::chrono::steady_clock::now();
    g_state.floors_set = false;

    if (UserStatus != nullptr) {
        std::memcpy(g_state.floors_told, UserStatus->floor_reached, sizeof g_state.floors_told);
        g_state.floors_set = true;
    }

    std::snprintf(line, sizeof line, "split at tick %lld: player %d leaves floor %d %s; this copy (player %d) %s", (long long) g_state.tick, leaver,
                  (int) UserStatus->cur_floor + 1, to_picker ? "for the floor menu" : "anyway", NetplayLocalPlayer(), goes ? "leaves" : "stays");
    Log(line);

    if (goes && !Mutated("gate_merge_back")) {
        g_floor_leaving = selectMapNo * 256 + UserStatus->cur_floor;
    }

    NetplayGoApart(goes ? "this copy left the floor" : "the other player left the floor", leaver);
    slotsync::Begin(); // coop-worlds: live sync from what both hold now
    slotsync::BagDump("split");

    // coop-gateflow: the leaver's copy to the floor menu (its own player there is the game's; on the floor it picks, player 2's
    // copy plays her member again, as any floor she comes onto apart: g_become_due); the stayer's asked to go along.
    if (to_picker && goes) {
        NetplayGoToFloor(selectMapNo, -1);
        return 3;
    }
    if (to_picker) {
        if (!Mutated("stay_as_toan")) {
            CoopWorldsStayOnFloor();
        }
        AskAlong(leaver);
        return 0;
    }

    // Player 2 going on: its copy's player becomes player 2's member (a character change, which ends in play), and
    // the gate's script starts again once it has (CoopWorldsSimTick), from the gate.
    if (goes && leaver == 2) {
        g_gate_again.script = BtEventInfo.script_no;
        g_gate_again.ext = BtEventInfo.ext_memory;
        sceVu0CopyVector(g_gate_again.position, BtEventInfo.position);
        sceVu0CopyVector(g_gate_again.direction, BtEventInfo.direction);
        CoopWorldsStayOnFloor();

        if (CharaFrame != nullptr) {
            float at[4] = {g_gate_again.position[0], g_gate_again.position[1], g_gate_again.position[2], 1.0f};

            sceVu0CopyVector(CharaMain.pos, at);
            CharaFrame->SetPosition(at);
        }

        return 2; // not this tick: the change runs first
    }

    // coop-floorexit (Jep's 2026-10-27 run: Toan left the floor anyway, and Xiao, who had said no, became Toan at the exit):
    // player 2's copy staying plays on as her member where she stood, as it does when he leaves by a script's way out
    // (CoopWorldsStayOnFloor), not as the game's player, Toan, at the gate he left by. DC_WORLDS_MUTATE=stay_as_toan (twin):
    // as before.
    if (!goes && !Mutated("stay_as_toan")) {
        CoopWorldsStayOnFloor();
    }

    return goes ? 1 : 0;
}

// coop-gateflow (Jep's 2026-11-02 playtest of the gate vote: he left the dungeon from the gate's "next floor / back to town"
// window after she had said yes to the next floor, and she was left on the floor; then "Strongly considering just removing
// the floor transition cutscene in favor of just going back out to the floor picker and allowing players to back out of that
// menu if they want to go back to town", and "go that way and we'll see if we like it"): with game.coop_gate_picker (on), a
// floor's exit gate ("out") no longer runs its script (program 160: the walk into the gate, the fade, the escape window, then
// the next floor or the town). Its player leaves for the dungeon's floor menu (the entrance's, as from town: the dungeon's
// script 100 opens it, the prev floor -1), the next floor marked reached first so the menu lists it. A floor that needs both
// players is asked there, as any pick of the menu is (PickRefuseApart: the other is called). Circle at the menu goes back to
// town (CoopWorldsEntrancePad). Together the pair splits at the gate on the tick both copies run it (FloorSplit to_picker),
// with no question first; the one staying is asked whether to go along (AskAlong), and its yes takes its copy to its own
// floor menu. Apart, this copy alone goes. DC_WORLDS_MUTATE=gate_picker_off (twin): the gate as before, its vote and its
// script, whatever the setting.
bool GatePickerOn() {
    return On() && dc::platform::ConfigGet().coop_gate_picker && !Mutated("gate_picker_off");
}

// The next floor reached (the gate is open to it), as the gate's own floor change does (SetNowFloor), so the menu lists it.
void ReachNextFloor() {
    const int to = std::min<int>(UserStatus->cur_floor + 1, maxFloorTbl__3[selectMapNo] - 1);

    if (selectMapNo >= 0 && selectMapNo < 7 && to > UserStatus->floor_reached[selectMapNo] && !Mutated("picker_next_unlisted")) {
        UserStatus->floor_reached[selectMapNo] = (s8) to;
    }
    std::fprintf(stderr, "worlds: gateflow: dungeon %d's floor menu lists floors 1 to %d on this copy (player %d)\n", (int) selectMapNo,
                 selectMapNo >= 0 && selectMapNo < 7 ? (int) UserStatus->floor_reached[selectMapNo] + 1 : -1, Player());
    std::fflush(stderr);
}

// The go-along question on the copy that stays: its own player, beside the game, as a call (g_call_ask, kAlongTo), drawn and
// stepped by CallTick; the yes is this copy's alone (no answer goes back).
void AskAlong(int leaver) {
    if (Mutated("along_off")) {
        return;
    }
    g_call_ask = CallAsk{leaver, selectMapNo, kAlongTo, UserStatus->cur_floor};
    std::fprintf(stderr, "worlds: gateflow: player %d left floor %d for the floor menu: this copy (player %d) is asked to go along\n", leaver,
                 (int) UserStatus->cur_floor + 1, Player());
    std::fflush(stderr);
}

bool GateToPicker(int script_no) {
    if (!GatePickerOn() || mode != GAME_MODE_DUNGEON || UserStatus == nullptr || selectMapNo < 0 || selectMapNo >= 7 || GateEvent(script_no) == nullptr) {
        return false;
    }

    ReachNextFloor();
    gameTask = GAME_TASK_PLAY;
    g_vote = Vote{};

    if (g_state.apart || !g_state.up || NetplayRole() == 0 || coop::g_partner.chara == nullptr) {
        std::fprintf(stderr, "worlds: gateflow: this copy (player %d) takes the gate on floor %d alone: to the floor menu\n", Player(),
                     (int) UserStatus->cur_floor + 1);
        std::fflush(stderr);
        NetplayGoToFloor(selectMapNo, -1);
        return true;
    }

    CDungeonEventData *gate = GateEvent(script_no);
    const int          by = TestEnvInt("DC_WORLDS_TEST_GATE_BY", 0);
    const int          asker = by == 1 || by == 2 ? by - 1 : Dist2(coop::g_partner.pos, gate->pos) < Dist2(CharaMain.pos, gate->pos) ? 1 : 0;

    std::fprintf(stderr, "worlds: gateflow: player %d takes the gate on floor %d at link tick %lld: to the floor menu, no question\n", asker + 1,
                 (int) UserStatus->cur_floor + 1, (long long) g_state.tick);
    std::fflush(stderr);
    FloorSplit(asker, true);
    return true;
}

} // namespace

// coop-warmreset (WS7 step 1): a warm pair's job starts with no door vote under way or passed (a gate let through in the job before
// ran the same gate's script once unasked in the next) and the worlds' said-once lines unsaid. The rest of g_state is the worlds
// machine both copies count up alike (sequence numbers, ids, apart and peer fields); a pair at a job's start is together, and
// clearing those on a running link could part the copies, so they stay. DC_COOP_MUTATE=job_gate_kept (twin): the vote stays.
void CoopWorldsResetForJob() {
    if (!TestJobResetKept("job_gate_kept")) {
        g_vote = Vote{};
    }
    if (!TestJobResetKept("job_trace_kept")) {
        g_state.load_pull_said = false;
        g_state.logged_build_wait = false;
        g_state.logged_scene_merge = false;
        g_state.logged_town_scene_merge = false;
        g_state.logged_town_busy_wait = false;
        g_state.logged_busy_wait = false;
        g_state.pickup_logged = -1;
    }
}

void CoopWorldsSessionEnded() {
    const bool announced = g_state.announced; // (said once a run)

    g_state = State{};
    g_state.announced = announced;
    g_vote = Vote{};
    std::fprintf(stderr, "worlds: the session ended (Quit Game): the pair's state reset\n");
}

// The job's leftovers line's part: the vote as a job finds it.
void CoopWorldsJobLeftovers(char *out, std::size_t size) {
    std::snprintf(out, size, "vote %d/%d passed %d", (int) g_vote.active, g_vote.stage, g_vote.passed);
}

// coop-gateflow: her own prompt at the gate (coop_useprompt's OpenGatePrompt) goes to the floor menu as the game's gate does.
bool CoopWorldsGatePicker(int player, CDungeonEventData *gate) {
    if (gate == nullptr || gate->event == nullptr || !GatePickerOn() || g_vote.active) {
        return false;
    }

    if (!g_state.apart && g_state.up) {
        // (DC_WORLDS_TEST_GATE_BY's place: the one at her prompt)
        if (UserStatus == nullptr || mode != GAME_MODE_DUNGEON) {
            return false;
        }
        ReachNextFloor();
        g_vote = Vote{};
        std::fprintf(stderr, "worlds: gateflow: player %d takes the gate on floor %d at link tick %lld (her own Cross): to the floor menu, no question\n",
                     player, (int) UserStatus->cur_floor + 1, (long long) g_state.tick);
        std::fflush(stderr);
        FloorSplit(player - 1, true);
        return true;
    }

    return GateToPicker(gate->event->script_no);
}

bool CoopWorldsGateVote(int script_no) {
    // coop-skeldoor: his Cross at a message-only slot shows its message beside the game (coop_interact.cpp).
    if (coop::HisCrossBeside(script_no)) {
        return true;
    }

    // coop-gateflow: the gate to the floor menu (above).
    if (!g_vote.active && GateToPicker(script_no)) {
        return true;
    }

    if (!On() || g_state.apart || !g_state.up || NetplayLocalPlayer() == 0 || mode != GAME_MODE_DUNGEON || UserStatus == nullptr) {
        g_vote = Vote{};
        return false;
    }

    // coop-p2prompt: a vote asked beside the game holds no script; the one asked starting the same gate is a yes.
    if (g_vote.active && g_vote.beside) {
        if (g_vote.stage == 1 && script_no == g_vote.script_no) {
            VoteLog("the one asked takes the gate too: both leave");
            g_vote = Vote{};
        }

        return false;
    }

    if (!g_vote.active) {
        if (g_vote.passed == script_no) {
            g_vote.passed = -1;
            return false; // decided: the gate's script runs
        }

        CDungeonEventData *gate = GateEvent(script_no);

        // A test's own gate (netplay's DC_NETPLAY_NEXT_FLOOR) goes through unasked unless the test votes.
        bool test_gate = NetplayTestDrivingGate() && TestEnv("DC_WORLDS_TEST_VOTE") == nullptr;

        if (gate == nullptr || test_gate || Mutated("vote_noask") || coop::g_partner.chara == nullptr) {
            return false;
        }

        // Who started it: the player nearer the gate (DC_WORLDS_TEST_GATE_BY=1|2 says, for a test).
        int by = TestEnvInt("DC_WORLDS_TEST_GATE_BY", 0);
        int asker = by == 1 || by == 2 ? by - 1 : Dist2(coop::g_partner.pos, gate->pos) < Dist2(CharaMain.pos, gate->pos) ? 1 : 0;
        char line[160];

        // coop-floorgate (Jep: never disrupt the other player): a gathered floor's question is not put to a player in a menu; the
        // gate stays shut and the asker is told that player is busy.
        if (const int gather = GatherFor(selectMapNo, UserStatus->cur_floor + 1);
            gather != kGatherNone && coop::FullMenuOpenFor(2 - asker) && !Mutated("gather_closes_menu")) {
            std::snprintf(line, sizeof line, "player %d starts the exit gate at link tick %lld; %s needs both players, player %d is busy in a menu: not asked",
                          asker + 1, (long long) g_state.tick, FloorName(selectMapNo, UserStatus->cur_floor + 1), 2 - asker);
            VoteLog(line);
            std::snprintf(line, sizeof line, "Player %d is busy right now. %s needs both players", 2 - asker, FloorName(selectMapNo, UserStatus->cur_floor + 1));
            SetGatherNote(asker + 1, line, true);
            gameTask = GAME_TASK_PLAY;
            return true;
        }

        // coop-dungeonsuite (x-dungeon-suite gate -Other menu): the one asked with their menu open never heard the question (the
        // menu kept their pad) and the gate's script stood both games for the vote's 10 s and then the asker's. Their menu closes
        // as the question comes, as it does at a split. DC_WORLDS_MUTATE=vote_menu leaves it open (the twin).
        if (coop::FullMenuRunsFor(2 - asker) && !Mutated("vote_menu")) {
            coop::FullMenuEndForSplit();
            std::snprintf(line, sizeof line, "player %d's menu closes for the question", 2 - asker);
            VoteLog(line);
        }

        g_vote = Vote{true, asker, 1, script_no, kVoteTicks, VotePad(1 - asker).buttons, -1};
        g_vote.to = UserStatus->cur_floor + 1;
        g_vote.gather = GatherFor(selectMapNo, g_vote.to); // coop-floorgate

        if (g_vote.gather != kGatherNone) {
            std::snprintf(line, sizeof line, "player %d starts the exit gate at link tick %lld; %s needs both players (%s): asking player %d to join",
                          asker + 1, (long long) g_state.tick, FloorName(selectMapNo, g_vote.to),
                          g_vote.gather == kGatherScene ? "its opening scene is unseen" : "its boss is alive", 2 - asker);
        } else {
            std::snprintf(line, sizeof line, "player %d starts the exit gate at link tick %lld; asking player %d", asker + 1, (long long) g_state.tick,
                          2 - asker);
        }
        VoteLog(line);
        return true;
    }

    if (script_no != g_vote.script_no) {
        return false;
    }

    const int pick = AskerPick(); // coop-gatepopup: the asker's Circle (1); coop-gatevote: its LEAVE NOW (2)

    if (pick == 1) {
        g_vote = Vote{};
        gameTask = GAME_TASK_PLAY;
        return true;
    }

    int           asked = g_vote.stage == 1 ? 1 - g_vote.asker : g_vote.asker;
    std::uint16_t buttons = VotePad(asked).buttons;
    std::uint16_t pressed = buttons & ~g_vote.held;
    if (doorgate::InGrace(kVoteTicks - g_vote.ticks)) {
        pressed = 0; // coop-gatepopup: no answer in its first half second
    }
    int           test = TestAnswer();
    // A test answers alone (its gate hook presses Cross on pad 1 for the scripts' own questions).
    bool          scripted = TestEnv("DC_WORLDS_TEST_VOTE") != nullptr;
    bool          yes = scripted ? test > 0 : (pressed & kInputCross) != 0;
    bool          no = scripted ? test < 0 : (pressed & kInputCircle) != 0;

    if (!scripted && VoteKeysInWindow() && g_vote.stage == 1) {
        GatherKeys(pressed, &yes, &no);
    }

    g_vote.held = buttons;

    // coop-gatevote: the asker answers in its box (no leave-anyway question): LEAVE NOW goes alone, at once or after a no.
    if (!scripted && VoteKeysInWindow() && g_vote.stage == 2) {
        yes = pick == 2;
        no = false;
    }
    if (g_vote.stage == 1 && pick == 2) {
        g_vote.stage = 2;
        yes = true;
        no = false;
    }

    if (g_vote.stage == 1 && yes) {
        VoteLog("the other player says yes: both leave");
        g_vote.active = false;
        g_vote.passed = script_no;
        return false;
    }

    if (g_vote.stage == 1 && (no || --g_vote.ticks <= 0)) {
        // coop-floorgate: a gathered floor has no leave anyway: both stay, the asker told so, playing on (never held).
        // DC_WORLDS_MUTATE=gather_leave_anyway (twin): the asker is asked to leave anyway, as before.
        if (g_vote.gather != kGatherNone && !Mutated("gather_leave_anyway")) {
            GatherDeclined(!no);
            g_vote = Vote{};
            gameTask = GAME_TASK_PLAY;
            return true;
        }

        // DC_WORLDS_MUTATE=vote_ignore: a no is taken as a yes (the twin of the decline).
        if (Mutated("vote_ignore")) {
            VoteLog("the other player says no (ignored): both leave");
            g_vote.active = false;
            g_vote.passed = script_no;
            return false;
        }

        VoteLog(no ? "the other player says no: asking the one who started it" : "no answer: asking the one who started it");
        g_vote.stage = 2;
        g_vote.ticks = kVoteTicks;
        g_vote.held = VotePad(g_vote.asker).buttons;
        g_vote.cursor = 0;
        return true;
    }

    if (g_vote.stage == 2 && yes) {
        VoteLog("leave anyway: the pair splits");
        int asker = g_vote.asker;

        g_vote = Vote{};

        int split = FloorSplit(asker);

        if (split == 1) {
            return false; // this copy goes through the gate now
        }

        if (split == 0) {
            gameTask = GAME_TASK_PLAY; // this copy stays on the floor
        }

        return true; // 2: player 2's change of member runs first, then the gate (CoopWorldsSimTick)
    }

    if (g_vote.stage == 2 && (no || --g_vote.ticks <= 0)) {
        VoteLog("not leaving: both stay");
        g_vote = Vote{};
        gameTask = GAME_TASK_PLAY;
        return true;
    }

    return true;
}

// coop-p2prompt (Jep, 2026-10-27: no prompt pauses the other player): player 2's exit gate, asked from her own prompt, asks
// player 1 beside his running game: the task stays play, his stick still walks him and only his Cross and Circle answer
// (MenuFilterPort keeps them from the game while CoopWorldsVoteAsks(1)). Both yes: the gate's script runs for both, its
// escape window answered with her choice (the next floor or back to town). His no: she is asked to leave anyway (her copy goes on alone, FloorSplit). Stepped
// by CoopWorldsSimTick on both copies alike. DC_WORLDS_MUTATE=vote_held holds the gate's script for the vote, as before.
bool CoopWorldsGateAskable() {
    return On() && !g_state.apart && g_state.up && NetplayLocalPlayer() != 0 && mode == GAME_MODE_DUNGEON && UserStatus != nullptr &&
           coop::g_partner.chara != nullptr && !g_vote.active && !Mutated("vote_held");
}

bool CoopWorldsGateAskBeside(int asker, CDungeonEventData *gate, int answer) {
    if (!CoopWorldsGateAskable() || gate == nullptr || gate->event == nullptr) {
        return false;
    }

    char line[160];

    // coop-floorgate: as CoopWorldsGateVote, a gathered floor's question is not put to a player in a menu.
    if (answer == 1 && GatherFor(selectMapNo, UserStatus->cur_floor + 1) != kGatherNone && coop::FullMenuOpenFor(2 - asker) &&
        !Mutated("gather_closes_menu")) {
        std::snprintf(line, sizeof line, "Player %d is busy right now. %s needs both players", 2 - asker, FloorName(selectMapNo, UserStatus->cur_floor + 1));
        SetGatherNote(asker + 1, line, true);
        VoteLog("the gathered question beside the game is not asked: the other player is busy in a menu");
        return false;
    }

    if (coop::FullMenuRunsFor(2 - asker)) {
        coop::FullMenuEndForSplit();
        std::snprintf(line, sizeof line, "player %d's menu closes for the question", 2 - asker);
        VoteLog(line);
    }

    g_vote = Vote{true, asker, 1, gate->event->script_no, kVoteTicks, VotePad(1 - asker).buttons, -1};
    g_vote.beside = true;
    g_vote.to = UserStatus->cur_floor + 1;
    g_vote.gather = answer == 1 ? GatherFor(selectMapNo, g_vote.to) : kGatherNone; // coop-floorgate: the next floor (back to town is never gathered)
    g_beside_gate.ext = gate->event->ext_mem;
    g_beside_gate.answer = answer;
    sceVu0CopyVector(g_beside_gate.position, gate->pos);
    sceVu0CopyVector(g_beside_gate.direction, gate->dir);
    std::snprintf(line, sizeof line, "player %d starts the exit gate at link tick %lld; asking player %d beside the game (task %d)%s%s", asker + 1,
                  (long long) g_state.tick, 2 - asker, (int) gameTask, g_vote.gather != kGatherNone ? "; it needs both players: " : "",
                  g_vote.gather != kGatherNone ? FloorName(selectMapNo, g_vote.to) : "");
    VoteLog(line);
    return true;
}

bool CoopWorldsGateAsking() {
    return g_vote.active && g_vote.beside;
}

bool CoopWorldsVoteAsks(int player) {
    return g_vote.active && g_vote.beside && (g_vote.stage == 1 ? 1 - g_vote.asker : g_vote.asker) == player - 1;
}

namespace {

// pt1111c (Jep, 2026-11-11: "the item-use story scene requires ALL players"): the pick an item scene's vote holds (the item still
// in its bag: nothing is used until every player says yes), and who of the players other than its user said yes so far.
struct ItemPick {
    int  user = 0; // 1 up: whose pick
    int  slot = -1;
    int  item = -1;
    bool shared = false;
    bool yes[coop::kMaxPlayers + 1] = {};
};

ItemPick g_item_pick;

// A player linked with this copy on its floor now (the link carries players 1 and 2 today: the list grows with it).
bool ItemSceneLinked(int player) {
    return player >= 1 && player <= 2 && g_state.up && !g_state.apart;
}

// The other players still to say yes to the item scene: every linked player but its user who has not (the link carries
// one other player today; a third would be asked in turn).
int ItemSceneNextAsked() {
    for (int player = 1; player <= coop::kMaxPlayers; player++) {
        if (player != g_item_pick.user && ItemSceneLinked(player) && !g_item_pick.yes[player]) {
            return player;
        }
    }
    return 0;
}

} // namespace

// pt1111c (Jep, 2026-11-11, on the floor story audit's two item-use stories): Wise Owl Forest floor 9's event10_2 (program 300,
// the Killer Snake's switch: item 240 used there, game flag 48) and Shipwreck floor 17's gatekey_3 (program 400, the music
// box's key, game flag 28) start a story scene that needs ALL players. When any player's pick at the spot is the item the
// script wants, nothing is used yet: every other player is asked in the game's popup, beside the running game (the exit
// gate's vote, coop-p2prompt), the user waiting in its box (Circle cancels). Every yes: they are brought beside the spot
// and the slot's script runs with the pick as its window's answer on every copy alike (lockstep), the user's pad answering
// its prompts as P1 does story (StartWithAnswer's MenuNotePrompt). Any no, no answer in 10 s, or a player gone: nothing
// changed (the item kept, no flag, no scene), the user told, free to try again. Apart (another player not on this floor) it
// cannot gather here: the user is told and every other player is called to this floor (the floor gate's call); once there
// together, the user uses it again. Read from the floor story audit (debug\notes\floor-story-audit), not inferred at run time.
// DC_WORLDS_MUTATE=item_scene_off (twin): the pick starts the script at once, as before.
struct ItemScene {
    int map, floor, script, flag; // floor 0 up
};
constexpr ItemScene kItemScenes[] = {{1, 8, 300, 48}, {2, 16, 400, 28}};

bool CoopWorldsItemSceneAsk(int user, int slot, int item, bool shared) {
    if (static const bool trace = TestEnv("DC_COOP_TEST_ITEM_SCENE") != nullptr; trace) { // (x-item-scene: the guards as asked)
        std::fprintf(stderr, "worlds: test: item scene ask: user %d slot %d item %d, gather %d mode %d status %d save %d on %d has player %d (local %d) apart %d heard %d map %d floor %d\n",
                     user, slot, item, CoopWorldsStoryGather() ? 1 : 0, (int) mode, UserStatus != nullptr ? 1 : 0, SaveData != nullptr ? 1 : 0, On() ? 1 : 0,
                     NetplayHasPlayer() ? 1 : 0, NetplayLocalPlayer(), g_state.apart ? 1 : 0, g_state.peer_heard ? 1 : 0, (int) selectMapNo,
                     UserStatus != nullptr ? (int) UserStatus->cur_floor : -1);
    }
    if (!CoopWorldsStoryGather() || Mutated("item_scene_off") || mode != GAME_MODE_DUNGEON || UserStatus == nullptr || SaveData == nullptr ||
        NowEventMan == nullptr || slot < 0 || slot >= 96 || NowEventMan->event[slot].event == nullptr || !On() ||
        // cand1111b: apart the link is down and NetplayLocalPlayer() is 0, yet the session goes on (the apart branch below calls the
        // others); this guard refused it first, so a pick apart started the scene alone (proven: x-item-scene-apart's trace, "has
        // player 0 (local 0) apart 1 heard 1"). Not linked and not apart: no session, the pick is retail's.
        (!NetplayHasPlayer() && !g_state.apart)) {
        return false;
    }

    const int     script = NowEventMan->event[slot].event->script_no;
    const ItemScene *scene = nullptr;

    for (const ItemScene &s : kItemScenes) {
        if (s.map == selectMapNo && s.floor == UserStatus->cur_floor && s.script == script && SaveData->GetGameFlag(s.flag) == 0) {
            scene = &s;
        }
    }
    if (scene == nullptr) {
        return false;
    }

    char line[200];

    if (g_state.apart) {
        if (!g_state.peer_heard) {
            return false; // nobody else in the game: the user alone is every player
        }
        std::snprintf(line, sizeof line, "This scene needs every player. Player %d has been called", 3 - Player());
        SetGatherNote(Player(), line); // (apart: this copy's own player, whatever the picker calls it)
        std::snprintf(line, sizeof line, "item scene: player %d's item %d at script %d (game flag %d unset) apart: not used; player %d is called to %s", Player(),
                      item, script, scene->flag, 3 - Player(), FloorName(selectMapNo, UserStatus->cur_floor));
        Log(line);
        QueueProgress(kWhatCall, UserStatus->cur_floor, 0, selectMapNo, UserStatus->cur_floor);
        doorgate::WaitForAnswer(true);
        doorgate::g_sent = doorgate::SentCall{kWhatCall, UserStatus->cur_floor, 0, selectMapNo, UserStatus->cur_floor};
        return true;
    }

    if (!g_state.up) {
        return false;
    }

    if (g_vote.active) {
        SetGatherNote(user, "Another question is open. Try again in a moment", true);
        return true;
    }

    g_item_pick = ItemPick{user, slot, item, shared};

    const int asked = ItemSceneNextAsked();

    if (asked == 0) {
        g_item_pick = ItemPick{};
        return false;
    }

    // As the exit gate's beside question: a player in a menu is not asked (told busy); one with a menu still stepping has it closed.
    if (coop::FullMenuOpenFor(asked)) {
        std::snprintf(line, sizeof line, "Player %d is busy right now. This scene needs every player", asked);
        SetGatherNote(user, line, true);
        VoteLog("item scene: the other player is busy in a menu: not asked, nothing used");
        g_item_pick = ItemPick{};
        return true;
    }
    if (coop::FullMenuRunsFor(asked)) {
        coop::FullMenuEndForSplit();
    }

    g_vote = Vote{true, user - 1, 1, script, kVoteTicks, VotePad(asked - 1).buttons, -1};
    g_vote.beside = true;
    g_vote.gather = kGatherItem;
    g_vote.to = UserStatus->cur_floor;
    g_beside_gate.answer = 3; // (the asker's box and the question read it: a story scene here)
    sceVu0CopyVector(g_beside_gate.position, NowEventMan->event[slot].pos);
    sceVu0CopyVector(g_beside_gate.direction, NowEventMan->event[slot].dir);
    std::snprintf(line, sizeof line, "item scene: player %d's item %d at script %d (game flag %d unset) at link tick %lld: nothing used, asking player %d", user,
                  item, script, scene->flag, (long long) g_state.tick, asked);
    VoteLog(line);
    return true;
}

const char *CoopWorldsFloorName(int map, int to) {
    return FloorName(map, to);
}

bool CoopWorldsCallQuestion(int *from, int *map, int *to, int *floor, int *cursor) {
    if (g_call_ask.from == 0 || g_call_ask.shown == 0) {
        return false;
    }

    *from = g_call_ask.from;
    *map = g_call_ask.map;
    *to = g_call_ask.to;
    *floor = g_call_ask.floor;
    *cursor = g_call_ask.cursor;
    return true;
}

bool CoopWorldsGatherQuestion(int *asked, int *asker, int *map, int *to, int *gather, int *cursor, int *stage, int *where) {
    if (!g_vote.active || !VoteDrawnInWindow()) {
        return false;
    }

    *asked = g_vote.stage == 1 ? 2 - g_vote.asker : g_vote.asker + 1;
    *stage = g_vote.stage;
    *where = g_vote.beside ? g_beside_gate.answer : 0; // coop-gatevote: her prompt's pick (1 the next floor, 2 back to town)
    *asker = g_vote.asker + 1;
    *map = selectMapNo;
    *to = g_vote.to;
    *gather = g_vote.gather;
    *cursor = g_vote.cursor;
    return true;
}

// coop-doorgate: the door's call as a question (doorgate::Ask, above), its answer and its keys.
namespace {
namespace doorgate {

// The other copy's call, apart: asked here.
void AskApart(int asker, int row, bool cave) {
    if (!cave && !GeoramaRow(row) && (row < 0 || row >= (int) g_rooms.size())) {
        return;
    }
    Ask(Player(), asker, row, cave, true);
}

// coop-gatepopup: the asker withdrew the call (its Circle on the waiting box): the question closes, the one asked told. Together
// on both copies alike (the asker's agreed pad, CoopWorldsDoorQuestionPad); apart as the call's progress (c 1).
void Withdrawn(int asker) {
    Question &q = g_question;
    char      line[96];

    if (!q.open || q.asker != asker) {
        return;
    }
    g_release_cross = true;
    q.open = false;
    q.answered_for = q.cave ? -1 : q.row;
    q.answered_at = q.steps;
    std::snprintf(line, sizeof line, "doorgate: player %d withdraws the call (Circle): player %d's question closes", asker, q.asked);
    Log(line);
    std::snprintf(line, sizeof line, "Player %d cancelled the invite", asker);
    SetNote(q.asked, line, true);
}

// The asked player's answer (0 coming, 1 not now, 2 busy: not asked, 3 none), on the asker's screen.
void Answered(int asked, int row, bool cave, int answer) {
    g_release_cross = true; // (the asker's copy, apart too)
    char        line[200];
    char        scene[64];
    const char *where = cave ? "The Divine Beast Cave" : row >= 0 && row < (int) g_rooms.size() && std::strcmp(g_rooms[row].name, "i01h06") == 0 ? "The mayor's house" : "This house";

    if (GeoramaRow(row)) {
        std::snprintf(scene, sizeof scene, "%s's story scene", GeoramaTown(row));
        where = scene;
    }

    if (answer == 4) {
        std::snprintf(line, sizeof line, "Player %d is coming: in you both go", asked); // (together, a house: DoorYes)
    } else if (answer == 5) {
        std::snprintf(line, sizeof line, "Player %d is here: go in together", asked); // (together, the cave door: DoorYes)
    } else if (answer == 0) {
        std::snprintf(line, sizeof line, GeoramaRow(row) ? "Player %d is coming to %s" : "Player %d is coming to the door", asked, GeoramaRow(row) ? GeoramaTown(row) : "");
    } else if (answer == 1) {
        std::snprintf(line, sizeof line, "Player %d can't come now. %s needs both players", asked, where);
    } else if (answer == 2) {
        std::snprintf(line, sizeof line, "Player %d is busy right now. %s needs both players", asked, where); // (floorgate's wording)
    } else {
        std::snprintf(line, sizeof line, "Player %d didn't answer. %s needs both players", asked, where);
    }
    if (g_wait_answer != std::chrono::steady_clock::time_point{}) {
        WaitForAnswer(false); // coop-gatepopup: the apart asker's wait ends
    }
    SetNote(3 - asked, line, true);
}

// coop-gatepopup (Jep's 2026-10-28 playtest: "pressing YES does nothing": his yes did reach the asker, whose link line said
// "Player 2 is coming to the door" for 4 s, and nobody moved): together, a yes brings the one asked to the asker, both copies
// alike (the places are lockstep state), and a house's door then takes both in together, as both at the door would (the merge's
// way back in, CoopWorldsReenterBuilding: g_reentering, no split); at the cave door the asker's next Cross opens it for both. 4 (a
// house) or 5 (the cave) for Answered. DC_WORLDS_MUTATE=door_yes_note_only (twin): the note alone, as before.
int DoorYes(const Question &q) {
    float asker[4] = {}, asked[4] = {};

    if (Mutated("door_yes_note_only") || GeoramaRow(q.row) || Chara == nullptr || !coop::TownPartnerShown(q.asker == 1 ? asked : asker, nullptr)) {
        return 0;
    }
    Chara->GetPosition(q.asker == 1 ? asker : asked);

    // Beside the asker, on the side the one asked came from (open ground, as a rule), kBeside / 2 off.
    float dx = asked[0] - asker[0], dz = asked[2] - asker[2];
    const float d = std::sqrt(dx * dx + dz * dz);

    dx = d > 1.0f ? dx / d : 1.0f;
    dz = d > 1.0f ? dz / d : 0.0f;

    float to[4] = {asker[0] + dx * kBeside * 0.5f, asker[1], asker[2] + dz * kBeside * 0.5f, 1.0f};

    if (q.asked == 2) {
        coop::TownPlacePartner(to);
    } else {
        Chara->SetPosition(to);
        if (Chara->frame != nullptr) {
            Chara->frame->SetPosition(to);
        }
    }

    char line[200];

    if (!q.cave) {
        g_door_yes_building = InteriorId(g_rooms[q.row].name);
    }
    std::snprintf(line, sizeof line, "doorgate: player %d says come: put beside player %d at (%.0f %.0f %.0f), %s", q.asked, q.asker, to[0], to[1], to[2],
                  q.cave ? "the cave door opens at the asker's next Cross" : "both go in together");
    Log(line);
    return q.cave ? 5 : 4;
}

void Close(int answer) {
    Question &q = g_question;
    char      line[160];

    g_release_cross = true;

    q.open = false;
    q.answered_for = q.cave ? -1 : q.row;
    q.answered_at = q.steps;
    std::snprintf(line, sizeof line, "doorgate: player %d answers player %d's call: %s", q.asked, q.asker,
                  answer == 0 ? "coming" : answer == 1 ? "not now" : answer == 2 ? "busy" : "no answer");
    Log(line);

    // DC_WORLDS_MUTATE=door_answer_lost (twin): the asker is never told.
    if (Mutated("door_answer_lost")) {
        return;
    }
    if (q.apart) {
        QueueProgress(kWhatDoorAnswer, q.cave ? 0 : q.row, q.cave ? 1 : 0, answer, 0);
    } else if (const int yes = answer == 0 ? DoorYes(q) : 0; yes != 0) {
        Answered(q.asked, q.row, q.cave, yes);
    } else {
        Answered(q.asked, q.row, q.cave, answer);
    }
}

} // namespace doorgate
} // namespace

// coop-gatepopup3 (Jep's 2026-10-28 Steam playtest: "when P2 entered the dungeon it showed the prompt for p1 to join but p1
// couldn't hit any buttons"): apart, the call's keys and the waiting asker's Circle read this copy's game pad as the game reads
// it (port 0's status, MenuFilterPort, before the question's hold), not the raw first device (InputGetPad(0)), which need not
// carry the player's keys (a bound controller, the keyboard). Test hook DC_WORLDS_TEST_GAMEPAD_ANSWER=cross|circle: that key on
// the game's pad alone, 4 of every 60 frames while a call is asked here. DC_WORLDS_MUTATE=call_keys_raw (twin): the raw device.
std::uint16_t g_game_pad0 = 0;

void CoopWorldsGamePad0(int *buttons) {
    static const char *const answer = TestEnv("DC_WORLDS_TEST_GAMEPAD_ANSWER");

    if (answer != nullptr && g_call_ask.from != 0 && g_call_ask.shown >= 60 && g_call_ask.shown % 60 < 4) {
        *buttons |= std::strncmp(answer, "circle", 6) == 0 ? kInputCircle : kInputCross;
    }
    g_game_pad0 = (std::uint16_t) *buttons;
}

std::uint16_t CoopWorldsCallPadButtons() {
    return Mutated("call_keys_raw") ? InputGetPad(0).buttons : g_game_pad0;
}

// coop-gatepopup2: the floor menu's pick, apart (PickRefuseApart above), on the game's pad of this copy.
void CoopWorldsEntrancePad(int *buttons) {
    static bool cross_was = false;
    static bool circle_was = false;
    bool        cross = (*buttons & kInputCross) != 0;
    bool        pressed = cross && !cross_was;
    const bool  circle = (*buttons & kInputCircle) != 0;
    bool        circle_pressed = circle && !circle_was;

    cross_was = cross;
    circle_was = circle;
    if (!On() || mode != GAME_MODE_DUNGEON || BtGameModeFlag != BT_GAME_MODE_ENTRANCE_MENU || DEnterMenu.state != DUN_ENTER_SELECT) {
        return;
    }

    // coop-playfix: the other player's call asked over this menu (CallFree): its keys are the question's (CallTick reads the
    // game's pad as taken before this), none reach the menu.
    if (g_state.apart && g_call_ask.from != 0 && g_call_ask.shown > 0 && !Mutated("call_entrance_busy")) {
        *buttons = 0;
        return;
    }

    // coop-playfix test hook (DC_WORLDS_TEST_PICK_ON_PEER_WAYIN=1, x-door-gate -Case cavewayin): this menu's Cross is the hook's:
    // none from the pad, and one press, the first tick the other copy is heard on its way into this dungeon's entrance (its
    // splash, floor -2), so this pick's call reaches it there. coop-pickgate: =2 (x-door-gate -Case cavebothmenu), the first tick
    // it is heard at that entrance's floor menu (-1), apart. Its press goes on through the pick's own gate below, as a pad's.
    if (static const int wayin_test = TestEnvInt("DC_WORLDS_TEST_PICK_ON_PEER_WAYIN", 0); wayin_test != 0 && g_pick_go < 0) {
        static bool pressed_once = false;
        // coop-gatevote (x-door-gate cavewayin FLAKE, 11-02): the other copy's presences are read when they come, several in one
        // tick on a loaded PC, and its splash (-2) lasts a few: read here as g_state.peer, the splash was overwritten by the
        // floor menu's (-1) before this hook looked, and it never pressed. It reads the latch of every presence heard instead.
        const bool  way_in = g_state.apart && g_state.peer_heard && g_test_peer_entrance.map == selectMapNo &&
                            (wayin_test == 2 ? g_test_peer_entrance.menu : g_test_peer_entrance.splash);

        *buttons &= ~kInputCross & 0xFFFF;
        cross = pressed = false;
        if (!way_in || pressed_once) {
            return;
        }
        pressed_once = true;
        *buttons |= kInputCross;
        cross = pressed = cross_was = true;
        std::fprintf(stderr, "worlds: test: the floor menu's Cross as the other copy %s\n", wayin_test == 2 ? "is at the entrance's floor menu" : "comes into the entrance");
        std::fflush(stderr);
    }

    // The call answered coming: the menu's hand on that floor, and its Cross.
    if (g_pick_go >= 0) {
        char line[160];

        DEnterMenu.selected_floor = g_pick_go;
        *buttons |= kInputCross;
        std::snprintf(line, sizeof line, "apart: the floor menu picks %s (the other player said yes and follows)", FloorName(selectMapNo, g_pick_go));
        Log(line);
        g_pick_go = -1;
        return;
    }

    // coop-gateflow (Jep, 2026-11-02: "allowing players to back out of that menu if they want to go back to town"): with
    // game.coop_gate_picker, Circle at the floor menu goes back to the dungeon's town (retail's Circle there only refused), the
    // town the dungeon's own way home takes (movechara's exit fade table), by the restart a resync leaves by. Together both
    // copies take it on one tick; apart this copy alone. DC_WORLDS_MUTATE=picker_back_off (twin): Circle refuses, as retail.
    // DC_WORLDS_TEST_PICKER_BACK=N (x-gate-flow -Case back): the Nth frame of this copy's floor menu taking picks, one Circle.
    if (static const int back_test = TestEnvInt("DC_WORLDS_TEST_PICKER_BACK", 0); back_test > 0) {
        static int frames = 0;

        if (++frames == back_test) {
            circle_pressed = true;
            std::fprintf(stderr, "worlds: test: Circle at the floor menu (frame %d of its picks)\n", frames);
            std::fflush(stderr);
        }
    }

    if (circle_pressed && GatePickerOn() && !doorgate::Waiting() && !Mutated("picker_back_off") && DEnterMenu.dungeon >= 0 && DEnterMenu.dungeon < 7) {
        static const int kTownOf[7] = {0, 1, 0x13, 0x2A, 0x17, 0x26, 0x3C}; // (DUNGEON_DIVINE_BEAST_CAVE ... DUNGEON_DEMON_SHAFT)

        *buttons &= ~(kInputCircle | kInputCross) & 0xFFFF;
        GamePad.AutoRepeatOff();
        GamePad.MenuModeOff();
        std::fprintf(stderr, "worlds: gateflow: Circle at the floor menu of dungeon %d: this copy (player %d) goes back to town (map %d)\n",
                     (int) DEnterMenu.dungeon, Player(), kTownOf[DEnterMenu.dungeon]);
        std::fflush(stderr);
        NetplayGoToTown(kTownOf[DEnterMenu.dungeon]);
        return;
    }

    const int to = DEnterMenu.selected_floor;
    // coop-pickgate (Jep, 2026-11-01, on the 10-31 Local playtest: told P1 was busy, she picked B1 again "and P2 just entered the
    // dungeon ... without prompting P1 at all"): the other copy at this entrance's floor menu too, still apart, let the pick go
    // without a word to it, when that menu could not be asked (busy). It can now (coop-playfix: the call is asked over its floor
    // menu), so a floor that needs both asks it there as anywhere. DC_WORLDS_MUTATE=pick_both_here_free (twin): the other at its
    // menu is not asked, as before.
    const bool both_here = g_state.peer_heard && g_state.peer.mode == GAME_MODE_DUNGEON && g_state.peer.map == selectMapNo && g_state.peer.floor == -1 &&
                           Mutated("pick_both_here_free");

    if (!cross || !g_state.apart || !doorgate::OtherLinked() || both_here || GatherFor(selectMapNo, to) == kGatherNone || Mutated("pick_gate_off")) {
        return;
    }

    *buttons &= ~kInputCross & 0xFFFF; // (the menu never takes it)
    if (pressed && !doorgate::Waiting()) {
        PickRefuseApart(to);
    }
}

void CoopWorldsDoorQuestionPad(int *buttons) {
    doorgate::Question &q = doorgate::g_question;

    q.steps++;

    // coop-gatevote: the asker's Cross let go since the last answer (together its agreed pad, apart this copy's game pad).
    if (doorgate::g_release_cross && !q.open) {
        const bool          together = On() && g_state.up && !g_state.apart;
        const std::uint16_t mine = together && q.asker >= 1 ? VotePad(q.asker - 1).buttons : (std::uint16_t) *buttons;

        if ((mine & kInputCross) == 0) {
            doorgate::g_release_cross = false;
            Log("doorgate: the asker's Cross is let go: its door asks again at the next Cross");
        }
    }

    // coop-gatepopup: apart, this copy waiting for its call's answer: its own Circle withdraws the call (doorgate::WaitCircle).
    if (g_state.apart && doorgate::Waiting()) {
        doorgate::WaitCircle((std::uint16_t) *buttons);
    }
    if (!q.open) {
        return;
    }

    // Together and apart must not swap under it (a split or a merge asks again at the next shut door).
    const bool together = On() && g_state.up && !g_state.apart;

    if (q.apart == together || mode != GAME_MODE_EDIT) {
        q.open = false;
        Log("doorgate: the call's question closes unanswered (the pair split, merged or left the town)");
        return;
    }

    // coop-gatepopup2 (Jep's 2026-10-28 playtest: the cave's call drawn over P2's talk with the mayor): apart, a question whose
    // player is taken by something else (a talk, an event, a menu: CallFree) closes as busy at once, as the floor call's does,
    // never drawn over it. DC_WORLDS_MUTATE=talk_not_busy (twin): it stays.
    // Test hook (DC_WORLDS_TEST_TALK_AT=N): from step N of the question this copy's player counts as taken (as by a talk).
    static const int talk_at = TestEnvInt("DC_WORLDS_TEST_TALK_AT", -1);
    const bool       taken = !CoopWorldsPlayerFree() || (talk_at >= 0 && q.age >= talk_at);

    if (q.apart && taken && !Mutated("talk_not_busy")) {
        Log("doorgate: the asked player is taken by something else (a talk, an event or a menu): the question closes as busy");
        doorgate::Close(2);
        return;
    }

    // coop-gatepopup (Jep: the asker's waiting box, "press the cancel button, or circle ... to cancel the invite"): together the
    // asker's agreed Circle, past the grace, withdraws it on both copies alike. DC_WORLDS_MUTATE=call_cancel_off (twin): no cancel.
    if (together) {
        // Test hook (DC_WORLDS_TEST_ASKER_CIRCLE=N, both copies): the asker's Circle N steps after the question opened.
        static const int asker_circle = TestEnvInt("DC_WORLDS_TEST_ASKER_CIRCLE", -1);
        std::uint16_t    mine = VotePad(q.asker - 1).buttons;

        if (q.age == asker_circle) {
            mine |= kInputCircle;
            std::fprintf(stderr, "worlds: doorgate: test: player %d's Circle %d steps after the question opened\n", q.asker, asker_circle);
            std::fflush(stderr);
        }
        const std::uint16_t asker_pressed = doorgate::InGrace(q.age) ? 0 : mine & ~q.asker_held;

        q.asker_held = mine;
        if ((asker_pressed & kInputCircle) != 0 && !Mutated("call_cancel_off")) {
            if (NetplayLocalPlayer() == q.asker) {
                coop::SoundOwner sound(q.asker);

                SndSePlay(MENU_SOUND_REFUSE, -1, 0);
            }
            doorgate::Withdrawn(q.asker);
            return;
        }
    }

    // Together: the asked player's agreed pad, the same on both copies; apart: this copy's own (its game pad).
    std::uint16_t now = together ? VotePad(q.asked - 1).buttons : (std::uint16_t) *buttons;

    // coop-gatepopup test hook (DC_WORLDS_TEST_EARLY_PRESS=N, both copies): the asked player's Cross N steps after the question
    // opened, as a press that was on its way.
    static const int early = TestEnvInt("DC_WORLDS_TEST_EARLY_PRESS", -1);

    if (q.age == early && (q.held & kInputCross) == 0) {
        now |= kInputCross;
        std::fprintf(stderr, "worlds: doorgate: test: player %d's Cross %d steps after the question opened (%s)\n", q.asked, early,
                     doorgate::InGrace(q.age) ? "in its grace: nothing" : "taken");
        std::fflush(stderr);
    }

    const std::uint16_t pressed = doorgate::InGrace(q.age++) ? 0 : now & ~q.held; // (coop-gatepopup: nothing in its first half second)
    const bool          own = !together || q.asked == 1; // the game's pad on this copy is the asked player's

    q.held = now;
    if (own) {
        *buttons &= ~(kInputCross | kInputCircle | kInputUp | kInputDown) & 0xFFFF; // (the hand's keys are the question's)
    }

    const bool here = !together || NetplayLocalPlayer() == q.asked; // its sounds on the asked player's copy

    if ((pressed & (kInputUp | kInputDown)) != 0) {
        q.cursor = 1 - q.cursor;
        if (here) {
            coop::SoundOwner sound(q.asked);

            SndSePlay(MENU_SOUND_CURSOR, -1, 0);
        }
    }

    const bool yes = (pressed & kInputCross) != 0 && q.cursor == 0;
    const bool no = (pressed & kInputCircle) != 0 || ((pressed & kInputCross) != 0 && q.cursor == 1);

    if (yes || no) {
        if (here) {
            coop::SoundOwner sound(q.asked);

            SndSePlay(yes ? MENU_SOUND_CONFIRM : MENU_SOUND_REFUSE, -1, 0);
        }
        doorgate::Close(yes ? 0 : 1);
    } else if (--q.ticks <= 0) {
        doorgate::Close(3);
    }
}

bool CoopWorldsDoorAsks(int player) {
    const doorgate::Question &q = doorgate::g_question;

    return q.open && q.asked == player;
}

bool CoopWorldsDoorQuestion(int *asked, char *text, int size, int *cursor, int *key) {
    const doorgate::Question &q = doorgate::g_question;

    if (!q.open) {
        return false;
    }

    *asked = q.asked;
    *cursor = q.cursor;
    *key = ((q.asker * 64 + (q.cave ? 63 : q.row < 0 ? 56 - q.row : q.row)) * 2 + 1) * 4 + 3; // (never a floor question's key: those end in 0-2)
    CoopWorldsDoorQuestionText(q.asker, q.row, q.cave, text, size);
    return true;
}

// coop-gatepopup (Jep's 2026-10-28 playtest: the door's question wrapped as "...and / you / go in together. Come now?"): three
// short lines broken by hand, each well inside the modal's 280-wide wrap (coop::DrawYesNoPopup logs every line's width), then
// the bubble twin's two rows. DC_WORLDS_MUTATE=door_text_old (twin): the house question as Jep saw it wrap.
void CoopWorldsDoorQuestionText(int asker, int row, bool cave, char *text, int size) {
    if (Mutated("door_text_old") && !cave && !doorgate::GeoramaRow(row)) {
        std::snprintf(text, size, "Player %d is going into\n%s, and you\ngo in together. Come now?\nCome\nNot now", asker, doorgate::RoomName(row));
    } else if (doorgate::GeoramaRow(row)) {
        std::snprintf(text, size, "Player %d picked a story\nscene in %s.\nWatch it together?\nCome\nNot now", asker, doorgate::GeoramaTown(row));
    } else if (cave) {
        std::snprintf(text, size, "Player %d is at the\nDivine Beast Cave.\nGo in together now?\nCome\nNot now", asker);
    } else if (row >= 0 && row < (int) doorgate::g_rooms.size() && std::strcmp(doorgate::g_rooms[row].name, "i01h06") == 0) {
        std::snprintf(text, size, "Player %d is at the\nmayor's house door.\nGo in together now?\nCome\nNot now", asker);
    } else {
        std::snprintf(text, size, "Player %d is at a house\ndoor in %s.\nGo in together now?\nCome\nNot now", asker, doorgate::TownName(row));
    }
}

int CoopWorldsDoorRows() {
    return (int) doorgate::g_rooms.size();
}

namespace {

void BesideGateScript() {
    BtEventInfo.script_no = g_vote.script_no;
    BtEventInfo.ext_memory = g_beside_gate.ext;
    BtEventInfo.event_marker = true;
    BtEventInfo.action_mode = 1;
    sceVu0CopyVector(BtEventInfo.position, g_beside_gate.position);
    sceVu0CopyVector(BtEventInfo.direction, g_beside_gate.direction);
}

// Every other player said yes: they are brought beside the spot (beside its user), and the slot's script starts with the pick
// as its window's answer, on every copy alike (the picker's own start, coop::StartPickedItemScene).
void ItemSceneGo() {
    const ItemPick pick = g_item_pick;
    CDungeonEventData *slot = pick.slot >= 0 && pick.slot < 96 && NowEventMan != nullptr ? &NowEventMan->event[pick.slot] : nullptr;
    const float       *user_at = pick.user == 1 ? CharaMain.pos : coop::g_partner.pos;
    char               line[200];

    g_item_pick = ItemPick{};
    for (int player = 1; player <= coop::kMaxPlayers && slot != nullptr; player++) {
        if (player == pick.user || !ItemSceneLinked(player)) {
            continue;
        }

        sceVu0FVECTOR spot = {user_at[0], user_at[1], user_at[2], 1.0f};
        float         beside[3];

        coop::SpawnSecondSpot(slot->pos, 0.0f, user_at, beside);
        spot[0] = beside[0];
        spot[2] = beside[2];
        if (player == 1) {
            CharaMain.SetPosition(spot);
            sceVu0CopyVector(CharaMain.pos, spot);
        } else if (coop::g_partner.chara != nullptr) {
            sceVu0CopyVector(coop::g_partner.pos, spot);
            coop::g_partner.chara->frame->SetPosition(spot);
            coop::g_partner.chara->SetPosition(spot);
        }
        std::snprintf(line, sizeof line, "item scene: player %d is brought beside the spot at (%.0f %.0f)", player, spot[0], spot[2]);
        VoteLog(line);
    }

    const bool started = coop::StartPickedItemScene(pick.user, pick.slot, pick.item, pick.shared);

    std::snprintf(line, sizeof line, "item scene: every player said yes: player %d's item %d starts script %d (%s)", pick.user, pick.item,
                  slot != nullptr && slot->event != nullptr ? (int) slot->event->script_no : -1, started ? "started" : "could not start");
    VoteLog(line);
}

// A no, no answer in time, the other copy gone or the user's own cancel: nothing changed (the item kept, no flag, no scene),
// the user told. They can use it again.
void ItemSceneCancel(const char *why, int other) {
    char line[160];

    std::snprintf(line, sizeof line, "item scene: %s: nothing used, player %d's item %d kept", why, g_item_pick.user, g_item_pick.item);
    VoteLog(line);
    if (other > 0) {
        std::snprintf(line, sizeof line, "Player %d %s. This scene needs every player", other,
                      std::strcmp(why, "no answer") == 0 ? "did not answer" : std::strcmp(why, "a player left") == 0 ? "left" : "said no");
        SetGatherNote(g_item_pick.user, line, true);
    }
    g_item_pick = ItemPick{};
    g_vote = Vote{};
}

void StepBesideVote() {
    if (!g_vote.active || !g_vote.beside) {
        return;
    }

    if (g_state.apart || !g_state.up || mode != GAME_MODE_DUNGEON) {
        // DC_WORLDS_MUTATE=item_scene_drop_ignored (twin): dropped quietly as the gate's question is (the user never told).
        if (g_vote.gather == kGatherItem && !Mutated("item_scene_drop_ignored")) {
            ItemSceneCancel("a player left", 2 - g_vote.asker); // (the link down or apart: the asked player is gone)
            return;
        }
        VoteLog("the question beside the game is dropped (apart, unlinked or off the floor)");
        g_vote = Vote{};
        return;
    }

    const int pick = AskerPick(); // coop-gatepopup: the asker's Circle (1); coop-gatevote: its LEAVE NOW (2)

    if (pick == 1) {
        if (g_vote.gather == kGatherItem) {
            ItemSceneCancel("its user cancelled", 0);
            return;
        }
        g_vote = Vote{};
        return;
    }

    int           asked = g_vote.stage == 1 ? 1 - g_vote.asker : g_vote.asker;
    std::uint16_t buttons = VotePad(asked).buttons;
    std::uint16_t pressed = buttons & ~g_vote.held;
    if (doorgate::InGrace(kVoteTicks - g_vote.ticks)) {
        pressed = 0; // coop-gatepopup: no answer in its first half second
    }
    int           test = TestAnswer();
    bool          scripted = TestEnv("DC_WORLDS_TEST_VOTE") != nullptr;
    bool          yes = scripted ? test > 0 : (pressed & kInputCross) != 0;
    bool          no = scripted ? test < 0 : (pressed & kInputCircle) != 0;

    if (!scripted && VoteKeysInWindow() && g_vote.stage == 1) {
        GatherKeys(pressed, &yes, &no);
    }

    g_vote.held = buttons;

    // coop-gatevote: the asker answers in its box (no leave-anyway question): LEAVE NOW goes alone, at once or after a no.
    if (!scripted && VoteKeysInWindow() && g_vote.stage == 2) {
        yes = pick == 2;
        no = false;
    }
    if (g_vote.stage == 1 && pick == 2) {
        g_vote.stage = 2;
        yes = true;
        no = false;
    }

    // pt1111c: an item scene: this player's yes counted; the next player still to answer is asked, and once every other player
    // said yes the scene starts. Any no or no answer drops it, nothing used. DC_WORLDS_MUTATE=item_scene_no_ignored (twin): a
    // no is taken as a yes, as the gate's vote_ignore.
    if (g_vote.gather == kGatherItem && g_vote.stage == 1 && (yes || ((no || g_vote.ticks <= 1) && Mutated("item_scene_no_ignored")))) {
        g_item_pick.yes[asked + 1] = true;
        if (const int next = ItemSceneNextAsked(); next > 0) {
            g_vote.ticks = kVoteTicks;
            g_vote.held = VotePad(next - 1).buttons;
            g_vote.cursor = 0;
            return;
        }
        ItemSceneGo();
        g_vote = Vote{};
        return;
    }
    if (g_vote.gather == kGatherItem && g_vote.stage == 1 && (no || --g_vote.ticks <= 0)) {
        ItemSceneCancel(no ? "said no" : "no answer", asked + 1);
        return;
    }

    if (g_vote.stage == 1 && yes) {
        VoteLog("the other player says yes: both leave (the gate's script, its window answered)");
        BesideGateScript();
        g_vote.active = false;
        g_vote.beside = false;
        g_vote.passed = g_vote.script_no;
        coop::SetEscapeWindowAnswer(g_beside_gate.answer);
        gameTask = GAME_TASK_SCRIPT_START;

        // Both leave by her gate: the floor menu its "next floor" opens (program 100's entrance window) is hers to answer.
        if (g_vote.asker == 1 && !Mutated("vote_p1floor")) {
            coop::MenuNotePrompt(true);
        }

        return;
    }

    if (g_vote.stage == 1 && (no || --g_vote.ticks <= 0)) {
        if (g_vote.gather != kGatherNone && !Mutated("gather_leave_anyway")) { // coop-floorgate (as CoopWorldsGateVote)
            GatherDeclined(!no);
            g_vote = Vote{};
            return;
        }

        VoteLog(no ? "the other player says no: asking the one who started it" : "no answer: asking the one who started it");
        g_vote.stage = 2;
        g_vote.ticks = kVoteTicks;
        g_vote.held = VotePad(g_vote.asker).buttons;
        g_vote.cursor = 0;
        return;
    }

    if (g_vote.stage == 2 && yes) {
        VoteLog("leave anyway: the pair splits");
        int asker = g_vote.asker;

        BesideGateScript();
        g_vote = Vote{};

        int split = FloorSplit(asker);

        if (split == 2 || split == 1) {
            coop::SetEscapeWindowAnswer(g_beside_gate.answer); // the leaver's copy: its gate's window, answered by her choice
        }

        if (split == 1) {
            gameTask = GAME_TASK_SCRIPT_START;
        }

        return;
    }

    if (g_vote.stage == 2 && (no || --g_vote.ticks <= 0)) {
        VoteLog("not leaving: both stay");
        g_vote = Vote{};
    }
}

} // namespace

// coop-gatepopup (Jep, 2026-10-28: "the player that initiated the event should probably be considered in a pause mode during it,
// same with the player that is receiving the event notice"): while a gate's question waits, the one who asked and the one asked
// stand as in their own pause (their pads reach nothing but the question's keys; on a floor, a zone where every player is in a
// pause, a menu or a question holds, coop_pause). Together both copies alike (the question is lockstep state): the door's call, the
// floor gate's vote. Apart this copy's own player: asked (the door's call, the floor call) or waiting for its call's answer.
// coop-gatevote (Jep, 2026-11-02): a gate's vote holds the asker throughout (its waiting box) and the one asked only while its
// question is up: after a no that player plays on, and the floor with it. DC_WORLDS_MUTATE=vote_stage2_holds (twin): both held.
// port 0: the game's player on this copy (together player 1); 1: player 2's partner (together). DC_WORLDS_MUTATE=question_no_hold
// (twin): both walk on, as before.
bool CoopWorldsQuestionHolds(int port) {
    if (coop::PromptHolds(port)) { // coop-townwheel: the swap question holds both players as a gate's does
        return true;
    }
    if (!On() || Mutated("question_no_hold")) {
        return false;
    }

    const doorgate::Question &q = doorgate::g_question;

    if (g_state.up && !g_state.apart) {
        const int player = port + 1;

        return (q.open && !q.apart && (q.asked == player || q.asker == player)) || (g_vote.active && (port == g_vote.asker || (port == 1 - g_vote.asker && (g_vote.stage == 1 || Mutated("vote_stage2_holds")))));
    }
    if (!g_state.apart || port != 0) {
        return false;
    }
    if (doorgate::g_wait_answer != std::chrono::steady_clock::time_point{} && std::chrono::steady_clock::now() >= doorgate::g_wait_answer) {
        doorgate::WaitForAnswer(false); // (no answer came: given up)
    }
    return (q.open && q.apart) || (g_call_ask.from != 0 && g_call_ask.shown > 0) || doorgate::g_wait_answer != std::chrono::steady_clock::time_point{};
}

// coop-gatepopup (Jep, 2026-10-28: "the player that initiated should have the same box that shows up on the receiving players
// except it shouldn't say yes or no, it should say something like Waiting for players response... and have an indication that you
// can press the cancel button, or circle"): whether this copy's player waits on a gate's question it asked (the door's call or
// the gathered floor gate's, together; its call apart), and who was asked. DC_WORLDS_MUTATE=wait_box_off (twin): no box, the link
// line's "Waiting for player N..." as before.
bool CoopWorldsWaitingFor(int *asked) {
    const doorgate::Question &q = doorgate::g_question;
    const int                 local = NetplayLocalPlayer();

    if (!On() || Mutated("wait_box_off")) {
        return false;
    }
    if (g_state.up && !g_state.apart && local != 0) {
        if (q.open && !q.apart && local == q.asker) {
            *asked = q.asked;
            return true;
        }
        return false;
    }
    if (g_state.apart && doorgate::Waiting()) {
        *asked = 3 - Player();
        return true;
    }
    return false;
}

bool CoopWorldsGateWaiting(int *waiting_on, int *state, int *where, int *to, bool *can_leave, int *cursor) {
    const int local = NetplayLocalPlayer();

    if (!On() || Mutated("wait_box_off") || !g_state.up || g_state.apart || local == 0 || !g_vote.active || local != g_vote.asker + 1 ||
        !VoteKeysInWindow()) {
        return false;
    }
    *waiting_on = 2 - g_vote.asker;
    *state = g_vote.stage == 1 ? 0 : 2;
    *where = g_vote.beside ? g_beside_gate.answer : 0;
    *to = g_vote.to;
    *can_leave = g_vote.gather == kGatherNone;
    *cursor = *can_leave ? g_vote.wait_cursor : 1;
    return true;
}

const char *CoopWorldsStatus() {
    // coop-gatepopup: the asker waits in its own box (coop::DrawWaitingBox), not on the link line.
    if (int waiting_on = 0; CoopWorldsWaitingFor(&waiting_on)) {
        return "";
    }
    if (int on = 0, state = 0, where = 0, to = 0, cursor = 0; [&] { bool leave = false; return CoopWorldsGateWaiting(&on, &state, &where, &to, &leave, &cursor); }()) {
        return "";
    }
    // (wait_box_off: the asker of a call, held for its answer, on that player's copy)
    if (const doorgate::Question &q = doorgate::g_question; q.open && !q.apart && Player() == q.asker && !Mutated("question_no_hold")) {
        static char waiting[64];

        std::snprintf(waiting, sizeof waiting, "Waiting for player %d...", q.asked);
        return waiting;
    }
    if (g_state.apart && doorgate::g_wait_answer != std::chrono::steady_clock::time_point{}) {
        static char waiting[64];

        std::snprintf(waiting, sizeof waiting, "Waiting for player %d...", 3 - Player());
        return waiting;
    }

    if (g_vote.active) {
        static char question[160];
        int         asked = g_vote.stage == 1 ? 1 - g_vote.asker : g_vote.asker;

        if (NetplayLocalPlayer() != asked + 1) {
            std::snprintf(question, sizeof question, "Waiting for player %d...", asked + 1);
        } else if (VoteDrawnInWindow()) {
            return ""; // coop-floorgate, coop-gatevote: asked in the game's own window (coop::DrawGatherQuestion)
        } else if (g_vote.stage == 1 && g_vote.gather != kGatherNone) {
            // DC_WORLDS_MUTATE=gather_hud_text (twin): the question on the link line, as first built.
            std::snprintf(question, sizeof question, "Player %d wants to go to %s. Join?  Cross yes / Circle no  (%d)", g_vote.asker + 1,
                          FloorName(selectMapNo, g_vote.to), (g_vote.ticks + 59) / 60);
        } else if (g_vote.stage == 1) {
            std::snprintf(question, sizeof question, "Player %d is leaving the floor. Leave too?  Cross yes / Circle no  (%d)", g_vote.asker + 1,
                          (g_vote.ticks + 59) / 60);
        } else {
            std::snprintf(question, sizeof question, "Player %d declines. Leave anyway?  Cross yes / Circle no  (%d)", 2 - g_vote.asker,
                          (g_vote.ticks + 59) / 60);
        }

        return question;
    }

    // coop-floorgate: the asker of a gate that did not open, on that player's copy.
    if (g_gather_note.player != 0 && g_gather_note.player == Player() &&
        std::chrono::steady_clock::now() - g_gather_note.at < std::chrono::seconds(4)) {
        return g_gather_note.text;
    }

    // coop-doorgate: a gated door's note or call, on that player's copy.
    if (const char *note = doorgate::NoteFor(Player()); note != nullptr) {
        return note;
    }

    if (CoopWorldsApart() && g_state.held_event >= 0 && std::chrono::steady_clock::now() - g_state.held_at < std::chrono::seconds(4)) {
        return "Toan needs to be here";
    }

    return CoopWorldsApart() ? g_state.status : "";
}

namespace {

// The town scripts' external functions that write story progress (editloop3.cpp's table): _SSET_GAME_FLAG
// (700), _SSET_GAME_INT_FLAG (711) and _SSET_PARTY_NUM (710, a character joining). Which of the game's
// events count as story is read from these alone (inferred: no event carries a "story" mark of its own).
constexpr int kStoryExts[] = {700, 711, 710};

int StoryFlags(const void *program, int event_no, int *flags, int max) {
    int count = 0;

    for (int ext : kStoryExts) {
        int got = ScriptExtCalls(static_cast<const RS_PROG_HEADER *>(program), event_no, ext, flags != nullptr && count < max ? flags + count : nullptr,
                                 count < max ? max - count : 0);
        count += got;
    }

    return count;
}

// Whether a town event's only ways out are _MAP_JUMPs (15), none to a constant map below 200 (a town), and it goes into
// no building (_GOTO_INTERIOR, 6).
bool IntoDungeonOnly(const void *program, int event_no) {
    const auto *prog = static_cast<const RS_PROG_HEADER *>(program);
    int         maps[4] = {-1, -1, -1, -1};
    int         jumps = ScriptExtCalls(prog, event_no, 15, maps, 4);

    if (jumps == 0 || jumps > 4 || ScriptExtCalls(prog, event_no, 6, nullptr, 0) != 0) {
        return false;
    }

    // -1: not a constant (Norune's entrance jumps to its world map's pick, 130's only jump).
    for (int i = 0; i < jumps; i++) {
        if (maps[i] != -1 && (maps[i] < 200 || maps[i] >= 300)) {
            return false;
        }
    }

    return true;
}

// coop-closet: whether an event is a scripted pickup, no story: it gives an item (_SITEM_GET, 702), its only story writes
// are _SSET_GAME_FLAGs (700) of constant flags that no other program of the script reads (_SGET_GAME_FLAG, 701: the flag
// marks only its own pickup as taken), and it adds no character (710), sets no int flag (711), jumps to no map (15) and
// goes into no building (6). The mayor's closet in i01h06, event 12 (flag 49, a Prickly: Jep's 2026-10-25 rule, she opens
// it apart too) is one (inferred from the script's calls alone, as StoryFlags).
bool PickupOnly(const void *program, int event_no, int *flag_out) {
    const auto *prog = static_cast<const RS_PROG_HEADER *>(program);
    int         flags[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    const int   sets = ScriptExtCalls(prog, event_no, 700, flags, 8);

    if (sets == 0 || sets > 8 || ScriptExtCalls(prog, event_no, 702, nullptr, 0) == 0) {
        return false;
    }

    for (int ext : {710, 711, 15, 6}) {
        if (ScriptExtCalls(prog, event_no, ext, nullptr, 0) != 0) {
            return false;
        }
    }

    int nos[512];
    const int count = std::min(ScriptPrograms(prog, nos, 512), 512);

    for (int s = 0; s < sets; s++) {
        if (flags[s] < 0) {
            return false; // not a constant: any flag
        }

        for (int i = 0; i < count; i++) {
            int       reads[16];
            const int got = nos[i] == event_no ? 0 : std::min(ScriptExtCalls(prog, nos[i], 701, reads, 16), 16);

            for (int r = 0; r < got; r++) {
                if (reads[r] == flags[s] || reads[r] < 0) {
                    if (CoopDebug()) {
                        std::fprintf(stderr, "coop-debug: event %d's game flag %d is read by event %d: no pickup\n", event_no, flags[s], nos[i]);
                    }
                    return false;
                }
            }
        }
    }

    *flag_out = flags[0];
    return true;
}

bool HoldStory(const void *program, int event_no, bool arrival);

} // namespace

// coop-exitscene (Jep's 2026-10-27 run: "when P1 went back into the dungeon, P2's screen faded to black as if she were
// entering too, then returned to normal when his entry finished"): a town event whose only way out is into a dungeon
// (IntoDungeonOnly: Norune's entrance) splits the pair as it starts, on both copies on the same tick, so it runs (the walk
// in, its fade, the world map, its jump) on its starter's copy alone and the other plays on in town, as at the cave door's
// key (CaveDoorKey). Before, both ran it together until its _MAP_JUMP split them, and the other's screen showed all of it.
// DC_WORLDS_MUTATE=dungeon_event_together (twin): the split at its jump, as before. Returns whether this copy runs it.
// coop-doorgate: Norune's cave door while B1's opening scene is unseen (its game flag, NetplayFloorSceneFlag: the lesson), used
// by `asker` (Cross into it, or Square for its key) while the other player is linked but not beside it: it stays shut, the
// asker told so, the other called (doorgate::Refuse). Both copies alike together. Whether it stays shut.
// coop-gatepopup2 (Jep's 2026-10-28 playtest: B1's call came at Toan's Square on the shut door, before his key opened it; "the
// prompt should probably happen when toan selects B1"): the door no longer asks; B1's pick on the cave's floor menu does
// (CoopWorldsEntrancePad). DC_WORLDS_MUTATE=cave_gate_at_door (twin): the door asks, as train 16 built it.
bool CoopWorldsCaveDoorRefused(int asker) {
    if (!Mutated("cave_gate_at_door")) {
        return false;
    }
    if (!doorgate::Gated() || Mutated("cave_gate_off") || MapNo != 0) {
        return false;
    }

    const int flag = NetplayFloorSceneFlag(0, 0);

    if (flag < 0 || SaveData == nullptr || SaveData->GetGameFlag(flag) != 0) {
        return false;
    }

    if (doorgate::BothAtDoor()) {
        static std::int64_t logged = -100;

        if (g_state.tick > logged + 60) {
            Log("doorgate: both players at the cave door (B1's opening scene unseen): it opens");
        }
        logged = g_state.tick;
        return false;
    }

    doorgate::Refuse(asker, -1, true, flag);
    return true;
}

bool CoopWorldsIntoDungeonOnly(const void *program, int event_no) {
    return IntoDungeonOnly(program, event_no);
}

// coop-pt1109f: a town event that is a story scene, not a door (DoorEventApart's note): it writes story and is no way into a dungeon alone.
bool CoopWorldsStorySceneLeaves() {
    if (mode != GAME_MODE_EDIT || EdInteriorFlag != 0 || !g_script_exit || EdEventData == nullptr || EdEventInfo.event_no <= 0) {
        return false;
    }
    return DoorAsSceneForTest() || (StoryFlags(EdEventData, EdEventInfo.event_no, nullptr, 0) > 0 && !IntoDungeonOnly(EdEventData, EdEventInfo.event_no));
}

bool CoopWorldsStayedHere() {
    return g_state.apart && g_state.stayer;
}

bool CoopWorldsDungeonEventAlone(const void *program, int event_no) {
    // coop-doorgate: the cave door's event, the Cross or Square that would take its player in (its reach alone runs on: the "!").
    if (int flag = -1; program != nullptr && event_no > 0 && mode == GAME_MODE_EDIT && EdInteriorFlag == 0 && coop::TownDoorShutMessage(program, event_no, &flag) >= 0 &&
                       (EdPadDown(0x40, 1) != 0 || EdPadDown(0x80, 1) != 0) && IntoDungeonOnly(program, event_no) &&
                       CoopWorldsCaveDoorRefused(g_state.apart ? Player() : (g_town_starter == 2 ? 2 : 1))) {
        g_town_starter = 1;
        return false;
    }

    // coop-doorfix: while the cave door acts on the game's walker (retail's refusal, the walk in), the door's event his walk
    // starts in reach each tick does not run: its end (EdEventFinish) would set his motion back to 0 under the act.
    if (int flag = -1; program != nullptr && event_no > 0 && mode == GAME_MODE_EDIT && EdInteriorFlag == 0 && CoopTownCaveHoldsGamePad() &&
                       coop::TownDoorShutMessage(program, event_no, &flag) >= 0) {
        return false;
    }

    if (!On() || !g_state.up || g_state.apart || NetplayLocalPlayer() == 0 || mode != GAME_MODE_EDIT || EdInteriorFlag != 0 || program == nullptr ||
        event_no <= 0 || !IntoDungeonOnly(program, event_no) || Mutated("dungeon_event_together")) {
        return true;
    }

    // coop-dunfreeze (Jep's 2026-10-28 Steam run, run log 2026-10-28a: "P2 went into the dungeon and P1 froze"): a
    // door's event (Norune's cave door, 130) is started by Toan's walk on every tick he stands in its reach, Cross or not
    // (editloop's door point to "dungeon"), and its script reads the pad: it leaves only on its Cross, with its door open
    // (its flag set). Split at any start, his walk up to the door split the pair, his copy "left", the way out undone 2 s
    // later; she went in apart and his merge into her event left him black 64 s. Its other runs (no Cross, the door shut,
    // its Square) run on both together as before exitscene: none leaves the town. DC_WORLDS_MUTATE=dungeon_split_in_reach
    // (twin): split at every start, as before.
    if (int flag = -1; coop::TownDoorShutMessage(program, event_no, &flag) >= 0 && !Mutated("dungeon_split_in_reach")) {
        const bool cross = EdPadDown(0x40, 1) != 0; // the game's pad, as the script's _GET_PADDOWN (her door's Cross is put on it)
        const bool open = SaveData != nullptr && SaveData->GetGameFlag(flag) != 0;

        // coop-doorfix: a Cross there is its player's own: the shut door's message as their note, the opened door's walk in
        // beside the game (the split when it ends); neither stops the other player.
        // coop-toandoor (fx51c's p2cave-p1zoning, red on main since 11-01): her Cross that took it ran no event, so the game's
        // pad goes back to Toan now, not kHerPadStart ticks on: her next Cross (her walk in holds her pad) was his, and his
        // walk standing at the mayor's door opened it (EdMoveChara's door point reads the game pad's Cross). DC_WORLDS_MUTATE=
        // cave_pad_kept (twin): the pad hers until "her event never ran", as before.
        if (cross && CoopTownCaveCross(g_town_starter == 2 ? 2 : 1, open)) {
            if (g_her_pad.on && !Mutated("cave_pad_kept")) {
                HerPadBack(open ? "her Cross at the cave door walks her in, no event" : "her Cross at the shut cave door is her note, no event");
            }
            g_town_starter = 1;
            return false;
        }

        if (!cross || !open) {
            static std::int64_t reach_tick = -2; // (logged once a stint: his walk runs it each tick in reach)

            if (g_state.tick > reach_tick + 1) {
                std::fprintf(stderr, "worlds: town event %d at its door, no way in this tick (Cross %d, door flag %d %s): no split, both run it\n", event_no,
                             (int) cross, flag, open ? "set" : "clear");
                std::fflush(stderr);
            }
            reach_tick = g_state.tick;
            return true;
        }
    }

    const int starter = g_town_starter == 2 ? 2 : 1;

    std::fprintf(stderr, "worlds: town event %d goes into a dungeon: the pair splits as it starts, player %d's copy runs it alone\n", event_no, starter);
    std::fflush(stderr);
    if (!CoopWorldsSplitForEvent(starter)) {
        return false;
    }
    if (starter == 2) {
        float at[3] = {};

        coop::TownPlacePlayerAtPartner(at); // her copy's Toan is hers from the split, where she stood
    }

    return true;
}

bool CoopWorldsHoldStoryEvent(const void *program, int event_no) {
    return HoldStory(program, event_no, false);
}

bool CoopWorldsPickupEvent(const void *program, int event_no) {
    int flag = -1;

    return program != nullptr && event_no > 0 && PickupOnly(program, event_no, &flag);
}

// coop-roomevent (Jep, 2026-10-27: "it has to be a solution that is more abstract"): a room event either player may start
// as their own: a scripted pickup (PickupOnly), or any event that writes no story (StoryFlags: no game flag set), adds no
// character and sets no int flag (710, 711), and takes nobody anywhere (no _MAP_JUMP, 15, no _GOTO_INTERIOR, 6): a pot, a
// chest, a shelf, a scripted spot with its "!". Story scenes stay the party's (Toan's).
bool CoopWorldsPartnerRoomEvent(const void *program, int event_no) {
    if (program == nullptr || event_no <= 0) {
        return false;
    }

    int flag = -1;

    if (PickupOnly(program, event_no, &flag)) {
        return true;
    }

    int flags[4] = {-1, -1, -1, -1};

    if (StoryFlags(program, event_no, flags, 4) != 0) {
        return false;
    }

    const auto *prog = static_cast<const RS_PROG_HEADER *>(program);

    for (int ext : {710, 711, 15, 6}) {
        if (ScriptExtCalls(prog, event_no, ext, nullptr, 0) != 0) {
            return false;
        }
    }

    return true;
}

// pt1111c (Jep's 11-11 playtest, item 19: the tester's copy crashed as Seda's duel began on Divine Beast Cave floor 8). Cause, from
// the dump (access violation in MulMatrix <- MotionProc2 <- SetMotionEX <- CCharacter::Step on CharaMain <- motionDrive <-
// DunMoveChara) and the log beside it: player 1 left the floor, player 2's copy stayed (CoopWorldsStayOnFloor) and its player
// became her member, Xiao (BecomeMember), who had not joined (party of one: a stand-in, coop_party.cpp); her walk then
// started the floor's marker event 300, whose scene and duel drive the event's main character (EdEventInfo.main_character,
// CharaMain) with Toan's event motions. Retail never runs a story script on anyone but a member who has joined (the cave's
// story is Toan's alone: Xiao joins after it), and coop-joinvisuals found the same crash on B1's elder scene ("her model as
// the player crashed"), which is why a floor's entrance keeps the game's player Toan and only draws her in his place. The
// guard: a dungeon script that writes story (StoryFlags: a game flag, an int flag or a character joining; a scripted pickup
// is none) does not start while the game's player is a stand-in. At the marker (marker true) it is simply not offered, so
// retail's fade and task never begin and the event's slot stays as it is (a merge carries it untouched); any other start
// (a monster's death scene) goes back to play at GAME_TASK_SCRIPT_START, the screen faded in if it had gone black. The floor's
// own start (program 100) is not one: its scenes play on the hidden Toan (CoopWorldsHideDungeonPlayer). The follow-up (below):
// apart, any player's copy but player 1's is held too, joined or not. The player is told
// "Toan needs to be here" (the town story guard's line). DC_WORLDS_MUTATE=standin_story (twin): it starts, as before.
bool CoopWorldsHoldStandInStory(int script_no, bool marker) {
    if (mode != GAME_MODE_DUNGEON || UserStatus == nullptr || script_no < 0 || script_no == 100 || EdEventScript.prog == nullptr) {
        return false;
    }

    const int  chara = UserStatus->cur_chara;
    const bool stand_in = chara < 0 || chara >= UserStatus->party_size;
    // pt1111c follow-up (the README's rule, "Story moments wait for player 1. Player 2 can't trigger story scenes alone"): apart,
    // a copy of any player but player 1 (her member joined or not; 3 players or more alike) starts no story event of a floor
    // either, as the town's story guard holds hers (HoldStory): player 1 starts it, the others watch it if they are there. Not
    // while a catch-up replays player 1's own play on this copy (his scenes ran on his copy: HoldStory's guard_in_catchup).
    // DC_WORLDS_MUTATE=apart_story (twin): only the stand-in's held, as the first fix had it.
    const bool apart_other = On() && g_state.apart && Player() != 1 && !NetplayCatchUnderWay() && !Mutated("apart_story");

    if (!stand_in && !apart_other) {
        return false; // player 1's copy (or together) with a member who has joined: retail's own case
    }

    int flags[4] = {-1, -1, -1, -1};
    const int sets = StoryFlags(EdEventScript.prog, script_no, flags, 4);
    int       pickup = -1;

    if (sets == 0 || PickupOnly(EdEventScript.prog, script_no, &pickup)) {
        return false;
    }

    const auto now = std::chrono::steady_clock::now();
    const bool fresh = script_no != g_state.held_event || now - g_state.held_at > std::chrono::seconds(4);

    if (Mutated("standin_story")) {
        if (fresh) {
            std::fprintf(stderr, "worlds: dungeon story event %d starts on stand-in %d (guard off): it sets %d story flag(s), first %d\n", script_no, chara, sets,
                         flags[0]);
            std::fflush(stderr);
            g_state.held_event = script_no;
            g_state.held_at = now;
        }
        return false;
    }

    if (fresh) {
        std::fprintf(stderr, "worlds: dungeon story event %d held at its %s: the player is %s %d (party of %d, player %d's copy, %s); Toan needs to be here (it sets %d story flag(s), first %d)\n",
                     script_no, marker ? "marker" : "start", stand_in ? "stand-in" : "member", chara, (int) UserStatus->party_size, Player(),
                     g_state.apart ? "apart" : "together", sets, flags[0]);
        std::fflush(stderr);
    }
    g_state.held_event = script_no;
    g_state.held_at = now;

    if (!marker) {
        BtEventInfo.script_no = -1;
        gameTask = GAME_TASK_PLAY;
        if (EdFadeOutCheck() != 0) {
            EdFadeIn(30, 0.0f, 0.0f, 0.0f);
        }
    }
    return true;
}

namespace {

// arrival: a room's arrival event (CoopWorldsHoldArrivalEvent), which no pickup is.
bool HoldStory(const void *program, int event_no, bool arrival) {
    // Test hook DC_WORLDS_TEST_EVENT_LOG=1 (o-opening-resync): each event this copy starts, once per new event number.
    static const bool event_log = TestEnvOn("DC_WORLDS_TEST_EVENT_LOG");
    static int        logged_event = -1;

    if (event_log && event_no != logged_event) {
        logged_event = event_no;
        std::fprintf(stderr, "worlds: test: event %d starts (player %d's copy, %s, mode %d)\n", event_no, Player(), g_state.apart ? "apart" : "together", (int) mode);
        std::fflush(stderr);
    }

    // coop-houseenter: a merge by her state (she was in the room first) runs her play on both copies until it is done: a story
    // scene of the room waits there on both alike (her copy as before; his replaying hers), and starts on both together
    // once merged. On her copy it started as her merge put Toan in the room, and her state set, which waits for a quiet
    // frame, waited on it for good. DC_WORLDS_MUTATE=story_in_her_merge (twin): her copy only, as before.
    const bool her_merge = NetplayStayerMerge() && !Mutated("story_in_her_merge");

    if (!On() || !g_state.apart || (Player() != 2 && !her_merge) || mode != GAME_MODE_EDIT || program == nullptr) {
        return false;
    }

    // coop-housemerge: a catch-up replays player 1's own play from the host's checkpoint (his story scenes too, as they ran
    // on his copy): held here, the mayor's gifts never reached his bag on her copy (house-after, 2026-10-25: the save parted
    // from the replay's first checksum). DC_WORLDS_MUTATE=guard_in_catchup (twin): held, as before.
    if (NetplayCatchUnderWay() && !her_merge && !Mutated("guard_in_catchup")) {
        return false;
    }

    int flags[4] = {-1, -1, -1, -1};
    int sets = StoryFlags(program, event_no, flags, 4);

    if (sets == 0) {
        return false;
    }

    // A way into a dungeon is no story prompt (Jep's 2026-10-21 rule: player 2 can always use things, but for
    // character-locked content and story prompts): an event that leaves the town only by its own _MAP_JUMP (15), to a
    // dungeon or a world map's pick, and goes into no building (_GOTO_INTERIOR, 6) starts on her copy, though it marks a flag
    // too (Norune's cave entrance, event 130, sets flag 1: Jep's 2026-10-24 playtest, "xiao still cant enter it").
    // DC_WORLDS_MUTATE=guard_entrance (twin): held as before.
    if (IntoDungeonOnly(program, event_no) && !Mutated("guard_entrance")) {
        std::fprintf(stderr, "worlds: town event %d goes into a dungeon: not held (it sets %d game flag(s), first %d)\n", event_no, sets, flags[0]);
        std::fflush(stderr);
        return false;
    }

    // coop-closet: a scripted pickup (a closet, a pot) is no story either: it starts on her copy, its item into her bag (the
    // game's pack holds hers apart), and its flag reaches his copy as flagsync carries any (FlagChanged), so his pickup is
    // taken too. DC_WORLDS_MUTATE=guard_pickup (twin): held as before ("she couldn't open the door").
    // coop-p2scene: a room's arrival is never one: the mayor's first visit (i01h06's 128) reads as one, as it gives items
    // and sets game flag 0, which no other event of the room reads.
    if (int pickup = -1; !arrival && PickupOnly(program, event_no, &pickup) && !Mutated("guard_pickup")) {
        if (event_no != g_state.pickup_logged) {
            g_state.pickup_logged = event_no;
            std::fprintf(stderr, "worlds: event %d is a pickup: not held (it gives an item and sets game flag %d, read by no other event)\n", event_no, pickup);
            std::fflush(stderr);
        }
        return false;
    }

    auto now = std::chrono::steady_clock::now();
    bool fresh = event_no != g_state.held_event || now - g_state.held_at > std::chrono::seconds(4);

    // The twin: the event starts as if Toan were here.
    if (Mutated("no_guard")) {
        static std::chrono::steady_clock::time_point logged{};

        if (now - logged > std::chrono::seconds(4)) {
            logged = now;
            std::fprintf(stderr, "worlds: story event %d not held (guard off): it sets %d game flag(s), first %d\n", event_no, sets, flags[0]);
            std::fflush(stderr);
        }
        return false;
    }

    if (fresh) {
        std::fprintf(stderr, "worlds: story event %d held: Toan needs to be here (it sets %d game flag(s), first %d)\n", event_no, sets, flags[0]);
        std::fflush(stderr);
    }

    g_state.held_event = event_no;
    g_state.held_at = now;
    return true;
}

} // namespace

// coop-p2scene (the house-enter thread, 2026-10-27: "She alone in the mayor's first-visit scene never leaves it"): a
// room's arrival event (EditInInit's start event, 128 unless the door names one) starts by itself, not by RunEvent, so
// the story guard never saw it, and her copy apart played the mayor's first visit: his supplies into her bag, game flag 0
// sent to his copy (so his own visit would have no scene), and then the scene waited in its event mode for good. The guard
// alone would not have held it either: the pickup rule (coop-closet) read it as a pickup. The story is player 1's (DESIGN: story events need P1 while apart): an arrival that would still write a story flag of
// this copy's that is not set yet is held as a walked-in story event is; one whose constant flags are all set (the scene
// seen) runs as before, as does any arrival that writes no story. DC_WORLDS_MUTATE=arrival_scene_apart (twin): it starts.
namespace {

// coop-mayorhouse (Jep's 2026-10-27 playtest: Xiao into the mayor's house first, no scene, but no mayor either till she
// opened the closet): the room's arrival also places its people. i01h06's 128 plays the first-visit scene only while game
// flag 0 is unset, and either way ends in the room's resting set-up (_CLEAR_VILLAGER_BUFF, _LOAD_IN_VILLAGER: the mayor),
// which the closet's event runs too. So her copy's held arrival now runs as if its story were seen: its unset story flags
// read as set to its script (CoopWorldsScriptGameFlag), and none of its flag writes land (CoopWorldsScriptWritesHeld), till
// it is over. The mayor stands there for her, a villager as after his scene; the scene, its flag and the supplies stay player
// 1's. DC_WORLDS_MUTATE=arrival_no_people (twin): held whole, as before.
struct ArrivalSeen {
    int  event = 0; // the arrival running as seen, or 0
    int  flags[8] = {};
    int  count = 0;
    Zone room;
    bool started = false;
    int  frames = 0;
};
ArrivalSeen g_arrival_seen;

int VillagersDrawn() {
    int drawn = 0;

    for (const CNPCharacter &villager : EdVillager) {
        drawn += villager.initialized != 0 && villager.draw_enabled != 0 ? 1 : 0;
    }
    return drawn;
}

// A room's arrival event that can still set a constant game flag this copy has not set (its story not seen here): its
// _SSET_GAME_FLAG flags in flags (up to 8), their count in sets.
bool ArrivalStoryUnset(const void *program, int event_no, int *flags, int *sets) {
    *sets = std::min(ScriptExtCalls(static_cast<const RS_PROG_HEADER *>(program), event_no, 700, flags, 8), 8); // _SSET_GAME_FLAG
    bool unset = false;

    for (int i = 0; i < *sets; i++) {
        unset = unset || (flags[i] >= 0 && SaveData->GetGameFlag(flags[i]) == 0);
    }
    return unset;
}

} // namespace

int CoopWorldsScriptGameFlag(int flag, int value) {
    if (g_arrival_seen.event > 0 && value == 0) {
        for (int i = 0; i < g_arrival_seen.count; i++) {
            if (g_arrival_seen.flags[i] == flag) {
                return 1;
            }
        }
    }
    return value;
}

bool CoopWorldsScriptWritesHeld() {
    return g_arrival_seen.event > 0;
}

// coop-scenejoin: a room's event about to start is a story scene both screens show from the game's camera, with her held and
// hidden (edit_in.cpp keeps the answer in its own statics, which the state set carries, so a copy that merged into the scene
// knows it too): the room's arrival while its story is not seen (the mayor's first visit). Asked before the event's first
// step, which may already write its flag. Other story events (a talk's scene) could join this rule later.
bool CoopWorldsSceneEvent(const void *program, int event_no) {
    int flags[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    int sets = 0;

    if (!On() || program == nullptr || event_no <= 0 || event_no != EdInteriorStartEvent || SaveData == nullptr) {
        return false;
    }
    if (ArrivalStoryUnset(program, event_no, flags, &sets)) {
        return true;
    }
    // coop-pt1109e (Jep's 11-09 playtest, 1l: both cameras wrong and her body in a finished house's scene): such a scene sets no game flag, only its part's completion event
    // (_SSET_CMP_EVENT; the door gate's table learned this in coop-pt1109c). Read as no story, it was no scene: her copy drew her own camera, still, through it, and his and her
    // copy drew her body standing in it. DC_WORLDS_MUTATE=scene_part_not_story (twin): game flags alone.
    if (Mutated("scene_part_not_story")) {
        return false;
    }
    int parts[4] = {-1, -1, -1, -1};
    const int count = std::min(ScriptExtCalls(static_cast<const RS_PROG_HEADER *>(program), event_no, 260, parts, 4), 4); // _SSET_CMP_EVENT
    for (int i = 0; i < count; i++) {
        if (parts[i] >= 0 && EditPartsInfo.CheckComplete(parts[i]) != 0 && EditPartsInfo.GetCompEvent(parts[i]) == 0) {
            return true;
        }
    }
    return false;
}

// (Not her room's arrival run as seen: its flag reads and writes are her copy's alone, never a state to send.)
// coop-soakfix3 (Jep 11-10, A: his merge never waits): her as-seen run does go in the set now (g_arrival_seen, snapshot-allow.txt),
// so his copy reads and holds the same flags she does through the rest of it; he is hidden while it finishes (her own event's
// deferred swap, 4e part 2) and his story arrival then plays for both (arrival_again). DC_WORLDS_MUTATE=seen_holds_merge (twin): the
// take waits for her run's end, as before (for good while she sits in it).
static bool ArrivalSeenHolds() {
    return g_arrival_seen.event != 0 && Mutated("seen_holds_merge");
}

bool CoopWorldsSceneTakeOk() {
    return !ArrivalSeenHolds() && !Mutated("scene_merge_off");
}

// coop-exitscene: a town's event outdoors (SceneOutdoors) on player 1's copy: his event or the town's own, not one player 2
// started (her pad holds the game's then), so her merge goes into his scene. DC_WORLDS_MUTATE=town_scene_merge_off (twin):
// the take waits for the event's end, as before (her screen black all through it).
bool CoopWorldsHerEventCarries() {
    if (!On() || Player() != 2 || !g_state.apart || mode != GAME_MODE_EDIT || Mutated("event_holds_merge") || Mutated("merge_into_her_event") || Mutated("merge_into_her_town_event")) {
        return false;
    }
    if (EdInteriorFlag != 0) {
        return CoopTownIndoorMode() == 3 || CoopTownIndoorMode() == 4; // ED_IN_MODE_TALK, ED_IN_MODE_EVENT
    }
    return GameMode == ED_MODE_EVENT || GameMode == ED_MODE_TALK;
}

bool CoopWorldsTownSceneTakeOk() {
    return (CoopTownSwapDeferred() || (Player() == 1 && g_town_starter != 2)) && !ArrivalSeenHolds() && !Mutated("town_scene_merge_off");
}

// coop-stage4 4e part 1: a talk under way on player 1's copy (a room's, mode 3, or the town's outdoors, ED_MODE_TALK) is a tick
// the state set may be taken on (SnapshotQuietTick): the talk is the game's globals and its script the VM's pinned arena, so the
// arriver lands into it at once (merge-no-wait). Not her copy's: her talk apart is hers alone (4e part 2). DC_WORLDS_MUTATE=
// talk_holds_merge (twin): the take waits for the talk's end, as before.
bool CoopWorldsTalkTakeOk() {
    return (CoopTownSwapDeferred() || Player() == 1) && !ArrivalSeenHolds() && !Mutated("talk_holds_merge");
}

bool CoopWorldsHoldArrivalEvent(const void *program, int event_no) {
    // coop-heldscene: player 1's copy too, coming into a room where she was first: the merge to come goes by her state (coop-
    // houseenter), which has no scene, and his copy would play it under his arrival hold (black) until the merge, which
    // waits on it. Held here, it plays after the merge on both copies (CoopWorldsArrivalAgain), or on his alone if she has
    // left the room by then. DC_WORLDS_MUTATE=held_scene_lost (twin): it starts on his copy, as before. (This is his arrival:
    // she, heard in this room already, was there first; his own zone and "there first" are not updated until his next tick.)
    const Zone room{GAME_MODE_EDIT, MapNo, InteriorId(EdInteriorName)};
    const bool his = Player() == 1 && g_state.peer_heard && g_state.peer == room && !Mutated("held_scene_lost");

    if (CoopDebug() && Player() == 1 && g_state.apart) {
        std::fprintf(stderr, "coop-debug: player 1 comes into %s: player 2 heard %d at mode %d map %d floor %d (this room %d)\n", EdInteriorName,
                     (int) g_state.peer_heard, g_state.peer.mode, g_state.peer.map, g_state.peer.floor, room.floor);
    }

    g_state.arrival_held = 0;

    // coop-doorgate: with the story gathered (game.coop_story_gather) a room's unseen story is gone into by both or by a player
    // whose partner is not linked (its door's gate): either way it plays as it is, its flag and gifts the party's (no "as
    // seen", no hold for player 1). DC_WORLDS_MUTATE=door_hold_kept (twin): held or as seen, as before.
    if (CoopWorldsStoryGather() && !Mutated("door_hold_kept")) {
        return false;
    }

    if (!On() || !g_state.apart || (Player() != 2 && !his) || program == nullptr || event_no <= 0 || SaveData == nullptr || Mutated("arrival_scene_apart")) {
        return false;
    }

    int        flags[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    int        sets = 0;
    const bool unset = ArrivalStoryUnset(program, event_no, flags, &sets);

    if (CoopDebug()) {
        std::fprintf(stderr, "coop-debug: room %s's arrival event %d sets %d game flag(s) (first %d), %s\n", EdInteriorName, event_no, sets, flags[0],
                     unset ? "one not set here" : "all set here");
    }

    if (!unset || (!his && !HoldStory(program, event_no, true))) {
        return false;
    }

    // coop-mayorhouse (above): on her copy, run as seen.
    if (!his && !Mutated("arrival_no_people")) {
        g_state.held_event = -1; // (nothing waits on Toan: no "Toan needs to be here")
        g_arrival_seen = ArrivalSeen{};
        g_arrival_seen.event = event_no;
        g_arrival_seen.room = room;
        for (int i = 0; i < sets && i < 8; i++) {
            if (flags[i] >= 0 && SaveData->GetGameFlag(flags[i]) == 0) {
                g_arrival_seen.flags[g_arrival_seen.count++] = flags[i];
            }
        }
        std::fprintf(stderr, "worlds: the room's arrival event %d runs as seen on player 2's copy (%s): its story (game flag %d) waits for player 1, the room's people come in\n",
                     event_no, EdInteriorName, g_arrival_seen.count > 0 ? g_arrival_seen.flags[0] : -1);
        std::fflush(stderr);
        return false;
    }

    g_state.arrival_held = event_no;
    g_state.arrival_held_zone = room;
    std::fprintf(stderr, "worlds: the room's arrival event %d held on player %d's copy (%s): %s\n", event_no, Player(), EdInteriorName,
                 his ? "player 2 is here first: it plays once the merge has her state" : "its story waits for player 1");
    std::fflush(stderr);
    return true;
}

// coop-heldscene (the coordinator, 2026-10-27: after her merge "P1 would never get the mayor's scene, key, or flag"): she
// came into the mayor's house alone first, her copy held its arrival (above), and he came in after her, his copy holding
// it too: the merge goes by her state (coop-houseenter, she was there first), a room whose arrival never played. The merge
// arms this on both copies on one tick; on the first free walk step after it, the room's arrival starts on both, if its
// story is still not seen (the game flags are the same on both by then). She is in the room and hidden through the scene
// as in any of his. His copy's own hold with no merge (she left the room first): it starts on his copy once she is gone.
// DC_WORLDS_MUTATE=held_scene_lost (twin): not armed, not held on his copy, as before.
// coop-soakfix3 (the carried run's end, proven on the i03h06 harness: both copies left the room together at link tick 1405 by its
// event's jump to the town, and neither ended her run there: its end was asked only from a room's step (CoopWorldsArrivalAgain,
// edit_in.cpp). Both kept masking the story's flags in town; his copy ended it when it next went into a room, 1 frame after
// coming in, and hers never: the copies then read the story's flags apart). Every link tick, every copy: a run whose room this
// copy is no longer in is over, on the same tick on every copy. DC_WORLDS_MUTATE=seen_ends_in_rooms (twin): only from a room's
// step, as before.
void CoopWorldsArrivalSeenOutTick() {
    // (once it has run in its room: on the way in, the room still loading, this copy is not in it yet; one that never starts ends by its
    // 600 frames, CoopWorldsArrivalAgain)
    if (g_arrival_seen.event <= 0 || !g_arrival_seen.started || Mutated("seen_ends_in_rooms")) {
        return;
    }
    const Zone in_room{GAME_MODE_EDIT, MapNo, mode == GAME_MODE_EDIT && EdInteriorFlag != 0 ? InteriorId(EdInteriorName) : -1};

    if (mode == GAME_MODE_EDIT && in_room == g_arrival_seen.room) {
        return;
    }
    std::fprintf(stderr, "worlds: the room's arrival event %d as seen is over on player %d's copy: out of its room at link tick %lld (mode %d)\n",
                 g_arrival_seen.event, Player(), (long long) NetplayLinkTick(), (int) mode);
    std::fflush(stderr);
    g_arrival_seen.event = 0;
}

int CoopWorldsArrivalAgain(const void *program, int event_no, int room_mode) {
    constexpr int kWalk = 0; // ED_IN_MODE_WALK

    // coop-mayorhouse: her arrival run as seen is over once the room walks after it started (room_mode -1: it is still to
    // start), or she is elsewhere; at most 600 frames (an arrival that never started).
    // coop-soakfix3: not at a merge any more (his merge goes into it, carrying it to his copy: the rest of it runs on both, the flags
    // read and held alike), and the 600 frames count only before it starts: one she sat in for 10 s ended mid-dialog, its reads of
    // the story's flags then the save's (0) under her open window. DC_WORLDS_MUTATE=seen_holds_merge (twin): as before.
    if (g_arrival_seen.event > 0) {
        const Zone in_room{GAME_MODE_EDIT, MapNo, EdInteriorFlag != 0 ? InteriorId(EdInteriorName) : -1};
        const bool old = Mutated("seen_holds_merge");
        const bool counts = old || !g_arrival_seen.started;
        const bool over = !(in_room == g_arrival_seen.room) || (old && !g_state.apart) || (g_arrival_seen.started && room_mode == kWalk) ||
                          (counts && ++g_arrival_seen.frames > 600);

        if (room_mode != kWalk) {
            g_arrival_seen.started = true;
        }
        if (over) {
            std::fprintf(stderr, "worlds: the room's arrival event %d as seen is over on player 2's copy after %d frames: %d villager(s) drawn, game flag %d = %d\n",
                         g_arrival_seen.event, g_arrival_seen.frames, VillagersDrawn(), g_arrival_seen.count > 0 ? g_arrival_seen.flags[0] : -1,
                         g_arrival_seen.count > 0 && SaveData != nullptr ? (int) SaveData->GetGameFlag(g_arrival_seen.flags[0]) : -1);
            // coop-soakfix3: why (the merge's carry must end it on every copy on one tick)
            std::fprintf(stderr, "worlds: (its end on player %d's copy: in its room %d (here mode %d map %d room %d, its %d %d %d), started %d, room mode %d, link tick %lld)\n",
                         Player(), in_room == g_arrival_seen.room ? 1 : 0, in_room.mode, in_room.map, in_room.floor, g_arrival_seen.room.mode,
                         g_arrival_seen.room.map, g_arrival_seen.room.floor, g_arrival_seen.started ? 1 : 0, room_mode, (long long) NetplayLinkTick());
            std::fflush(stderr);
            g_arrival_seen.event = 0;
        }
    }

    if (g_state.arrival_running > 0 && room_mode == kWalk) {
        std::fprintf(stderr, "worlds: the room's arrival event %d after her merge is over at link tick %lld, %lld ticks after the merge: game flag %d = %d\n",
                     g_state.arrival_running, (long long) g_state.tick, (long long) (g_state.tick - g_state.merge_tick), g_state.arrival_flag,
                     g_state.arrival_flag >= 0 && SaveData != nullptr ? (int) SaveData->GetGameFlag(g_state.arrival_flag) : -1);
        std::fflush(stderr);
        g_state.arrival_running = 0;
    }

    if (g_state.arrival_held > 0 && !(Here() == g_state.arrival_held_zone)) {
        g_state.arrival_held = 0;
    }

    // His copy's hold, she gone from the room before any merge: his scene, here alone.
    const bool alone = g_state.arrival_held > 0 && Player() == 1 && g_state.apart && !(g_state.peer == Here()) && !NetplayLinkUp() && room_mode == kWalk;

    if (!g_state.arrival_again && !alone) {
        return 0;
    }
    if (!alone && (!On() || g_state.apart || !(Here() == g_state.arrival_again_zone))) {
        g_state.arrival_again = false;
        return 0;
    }
    if (room_mode != kWalk) {
        return 0;
    }

    g_state.arrival_again = false;
    g_state.arrival_held = 0;

    int flags[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    int sets = 0;

    if (program == nullptr || event_no <= 0 || SaveData == nullptr || !ArrivalStoryUnset(program, event_no, flags, &sets)) {
        return 0;
    }

    int unset = flags[0];

    for (int i = 0; i < sets; i++) {
        if (flags[i] >= 0 && SaveData->GetGameFlag(flags[i]) == 0) {
            unset = flags[i];
            break;
        }
    }

    g_state.arrival_running = event_no;
    g_state.arrival_flag = unset;
    std::fprintf(stderr, "worlds: the room's arrival event %d runs again %s at link tick %lld, %lld ticks after it (%s): player 1's story\n", event_no,
                 alone ? "on his copy alone, she left before a merge" : "after her merge", (long long) g_state.tick,
                 (long long) (g_state.tick - g_state.merge_tick), EdInteriorName);
    std::fflush(stderr);
    return event_no;
}

namespace dc::game {
int FadeOutToEvent(int event_no, int level); // editloop
int GotoInterior(char *name, int entrance, int direction, ED_EVENT_PARAM *param, int start_event); // editloop
} // namespace dc::game

// coop-pt1109c test hook (DC_WORLDS_TEST_HER_ROOM=<room>): 60 ticks after a state file's load, both copies alike, that room's Georama part complete and its scene unseen, and
// her door into it asked for as her Cross at it would (CoopWorldsTakeInterior's gate).
static void HerRoomForTest() {
    static const char *const room = TestEnv("DC_WORLDS_TEST_HER_ROOM");
    static int               ticks = 0;
    static int               part = -1;
    static bool              done = false;

    if (room == nullptr || !NetplayStateFileLoaded()) {
        return;
    }
    // (coop-pt1109e: who holds what, once the scene let them out: the row's check on both copies)
    if (static bool dumped = false; done && !dumped && EditPartsInfo.GetCompEvent(part) != 0 && GameMode == ED_MODE_WALK && !g_state.apart) {
        dumped = true;
        CoopWorldsBagDump("the scene over");
    }
    if (!done && !g_state.apart && ++ticks >= 60) {
        done = true;
        doorgate::Scan();
        const int row = doorgate::Find(room);

        part = row >= 0 && doorgate::g_rooms[row].part_count > 0 ? doorgate::g_rooms[row].parts[0] : 0; // (the twin's table has none: the row's own)
        if (EDITPARTS_INFO *info = EditPartsInfo.GetPartsInfo(part)) {
            for (int i = 0; i < 6; i++) {
                if (info->elements[i].id >= 0) {
                    info->elements[i].enabled = 1;
                }
            }
        }
        EditPartsInfo.SetCompEvent(part, 0);
        std::fprintf(stderr, "worlds: test: part %d complete, its scene unseen; her door into %s at link tick %lld\n", part, room, (long long) g_state.tick);
        CoopWorldsTownEventBy(2);
        // coop-pt1109e: DC_WORLDS_TEST_HER_ROOM_ALONE=1: she takes the door ungated (a test's way in, g_mod_door), as her Cross there does with the story gather off or the pair apart first:
        // the split, and the scene on her copy alone.
        if (TestEnvOn("DC_WORLDS_TEST_HER_ROOM_ALONE")) {
            char name[32];

            std::snprintf(name, sizeof name, "%s", room);
            std::fflush(stderr);
            doorgate::g_mod_door = true;
            GotoInterior(name, part, 0, nullptr, -1);
            doorgate::g_mod_door = false;
            return;
        }
        if (!CoopWorldsDoorRefused(room, 2)) {
            std::fprintf(stderr, "worlds: test: her door into %s opens for her alone\n", room);
        }
        g_town_starter = 1;
        std::fflush(stderr);
    }
}

int CoopWorldsTestTownEvent(const void *program) {
    HerRoomForTest();
    static const char *const which = TestEnv("DC_WORLDS_TEST_EVENT");
    static const int         after = TestEnvInt("DC_WORLDS_TEST_EVENT_AFTER", 300);
    static bool              fired = false;

    // coop-doorgate test hook DC_WORLDS_TEST_GEORAMA_PICK=K:E (x-door-gate -Case georama): K town walk ticks after this copy
    // went apart, the Georama menu's Event pick of town event E, as editloop's EDIT_MENU_MODE_EVENT takes it (its gate, then
    // its fade), once.
    static const char *const pick = TestEnv("DC_WORLDS_TEST_GEORAMA_PICK");
    static int               pick_ticks = 0;
    static bool              picked = false;

    // (counted only while the other copy is heard from: just after the split it is not yet, and the pick would not be gated)
    if (pick != nullptr && !picked && On() && g_state.apart && program != nullptr && EdInteriorFlag == 0 && doorgate::OtherLinked() &&
        ++pick_ticks >= std::atoi(pick)) {
        const char *colon = std::strchr(pick, ':');
        const int   event_no = colon != nullptr ? std::atoi(colon + 1) : 0;

        picked = true;
        std::fprintf(stderr, "worlds: test: this copy (apart, player %d) picks Georama event %d\n", Player(), event_no);
        std::fflush(stderr);
        if (event_no > 0 && !CoopWorldsGeoramaEventRefused(event_no)) {
            FadeOutToEvent(event_no, 0);
        }
        return 0;
    }

    if (which == nullptr || fired || !On() || !g_state.apart || program == nullptr || ++g_state.town_ticks < after) {
        return 0;
    }
    // coop-pt1109c test hook (DC_WORLDS_TEST_EVENT_ON_HER_WAY=N): N town ticks after the other copy is heard inside its building (it comes out meanwhile, into the event).
    if (static const int other_way = TestEnvInt("DC_WORLDS_TEST_EVENT_ON_HER_WAY", -1); other_way >= 0) {
        static int heard = 0;

        if (heard == 0 && (!g_state.peer_heard || g_state.peer.floor <= 0)) {
            return 0;
        }
        if (++heard < other_way) {
            return 0;
        }
    }

    fired = true;

    int event_no = std::strcmp(which, "story") == 0 ? 0 : std::atoi(which);

    if (event_no == 0) {
        int nos[512];
        int count = ScriptPrograms(static_cast<const RS_PROG_HEADER *>(program), nos, 512);

        if (CoopDebug()) {
            std::fprintf(stderr, "worlds: test: the town's script has %d programs:", count);
            for (int i = 0; i < count && i < 512; i++) {
                std::fprintf(stderr, " %d:%d", nos[i], StoryFlags(program, nos[i], nullptr, 0));
            }
            std::fprintf(stderr, "\n");
        }

        for (int i = 0; i < count && i < 512 && event_no == 0; i++) {
            if (nos[i] > 0 && (nos[i] < 128 || nos[i] > 150) && StoryFlags(program, nos[i], nullptr, 0) > 0) { // 128-150: arrivals, the clock
                event_no = nos[i];
            }
        }
    }

    std::fprintf(stderr, "worlds: test: this copy (apart, player %d) starts town event %d (%d game flag set(s) in it)\n", Player(), event_no,
                 event_no > 0 ? StoryFlags(program, event_no, nullptr, 0) : 0);
    std::fflush(stderr);
    return event_no;
}

bool CoopWorldsMutated(const char *feature) {
    return Mutated(feature);
}

bool CoopWorldsTakeScriptExit(int next_map) {
    // Only a jump into a dungeon splits (another town's map jump keeps the pair together, as the map menu's does not
    // yet either). DC_WORLDS_MUTATE=no_script_exit lets it through on both copies, as before (twin).
    if (On() && g_state.up && NetplayLocalPlayer() != 0 && !g_state.apart) {
        std::fprintf(stderr, "worlds: a town event's own map jump to %d (the script's _MAP_JUMP; event %d, town mode %d)\n", next_map, (int) EdEventInfo.event_no, (int) GameMode);
        std::fflush(stderr);
    }

    if (next_map < 200 || next_map >= 300 || Mutated("no_script_exit")) {
        return true;
    }

    g_script_exit = true; // coop-pt1109f: what CoopWorldsStorySceneLeaves goes by: a script's own jump, the running event's
    const bool goes = CoopWorldsTakeExit(next_map);

    g_script_exit = false;
    return goes;
}

bool CoopWorldsTestHoldExit() {
    static const int hold = TestEnvInt("DC_WORLDS_TEST_HOLD_EXIT", 0);
    static int       held = 0;
    static int       held_split = -1;

    if (hold <= 0 || !On() || !g_state.apart || g_state.stayer || g_state.split_tick < 0) {
        return false;
    }

    if (held_split != (int) g_state.split_tick) {
        held_split = (int) g_state.split_tick;
        held = 0;
        std::fprintf(stderr, "worlds: test: this copy (player %d) holds its way out of town %d ticks\n", Player(), hold);
        std::fflush(stderr);
    }

    return ++held <= hold;
}

// The gate test's facing for Toan (NAN: none given).
float g_gate_yaw = NAN;

// With a test's warp to a town exit: DC_WORLDS_TEST_GAME_FLAG=F sets game flag F too (Norune's cave door, event 130,
// lets the player in once flag 1 is set, as in Jep's save; unset, it only says its message).
static void TestGameFlag() {
    static const int flag = TestEnvInt("DC_WORLDS_TEST_GAME_FLAG", -1);

    if (flag >= 0 && SaveData != nullptr && SaveData->GetGameFlag(flag) == 0) {
        SaveData->SetGameFlag(flag, 1);
        std::fprintf(stderr, "worlds: test: game flag %d set\n", flag);
    }
}

// coop-mergestand: the yaw of the last warp this walk step applies: Toan's stand's, else the test gate warp's.
static bool  g_stand_warp = false;
static float g_stand_yaw = 0.0f;
static bool  g_stand_placed = false; // coop-steamsplit: Toan's stand placed by a linked resync (both copies, one tick)

// coop-pt1111j test hook (DC_WORLDS_TEST_DOOR_AFTER_MERGE=K:pN:X:Z[:Y[:YAW]], both copies): K link ticks after the pair's
// first merge, together, player N (1 Toan, 2 her partner) is put at (X, Z), a door's step, on both copies on that one tick:
// a door taken within the merge's first 15 s (x-door-again). Once a run.
static bool DoorAfterMergeForTest(int who, float *pos) {
    static const char *const spec = TestEnv("DC_WORLDS_TEST_DOOR_AFTER_MERGE");
    static int               after = -1, player = 0;
    static float             x = 0.0f, z = 0.0f, y = 20.0f, yaw = NAN;
    static bool              parsed = false, fired = false;

    if (!parsed) {
        parsed = true;
        if (spec == nullptr || std::sscanf(spec, "%d:p%d:%f:%f:%f:%f", &after, &player, &x, &z, &y, &yaw) < 4) {
            after = -1;
        }
    }
    if (after < 0 || fired || who != player || !On() || !g_state.up || g_state.apart || g_state.merges < 1 || g_state.merge_tick < 0 ||
        g_state.tick < g_state.merge_tick + after) {
        return false;
    }
    fired = true;
    pos[0] = x;
    pos[1] = y;
    pos[2] = z;
    pos[3] = who == 1 ? 1.0f : (std::isnan(yaw) ? 0.0f : yaw);
    if (who == 1) {
        g_gate_yaw = yaw;
    }
    std::fprintf(stderr, "worlds: test: door after the merge: player %d to (%.0f %.0f) at link tick %lld (%lld after the merge at %lld)\n", who, x, z,
                 (long long) g_state.tick, (long long) (g_state.tick - g_state.merge_tick), (long long) g_state.merge_tick);
    std::fflush(stderr);
    return true;
}

float CoopWorldsTestGateYaw() {
    if (g_stand_warp) {
        g_stand_warp = false;
        return g_stand_yaw;
    }

    return g_gate_yaw;
}

bool CoopWorldsTestGateWarp(float *pos) {
    // coop-mergestand: Toan's stand after a town resync (the host's spot at the resync tick, in the resync's state): applied
    // here, the town's walk step, which both copies reach on the same tick; the town partner's load, which places her, can
    // come a tick apart between the copies, and Toan's position is in the lockstep checksum (w-spawn: OUT OF SYNC at tick
    // 60 after a join, a second resync).
    if (coop::SpawnToan his; On() && mode == GAME_MODE_EDIT && GameMode == ED_MODE_WALK && EdInteriorFlag == 0 && coop::SpawnTakeToan(MapNo, &his)) {
        pos[0] = his.pos[0];
        pos[1] = his.pos[1];
        pos[2] = his.pos[2];
        pos[3] = 1.0f;
        g_stand_yaw = his.yaw;
        g_stand_warp = true;
        g_stand_placed = g_state.up && !g_state.apart; // coop-steamsplit: the gate warp's "stand" trigger
        std::printf("coop: Toan placed where he stood (the walk step after the resync, link tick %lld) at (%.0f %.0f %.0f)\n", (long long) g_state.tick, pos[0],
                    pos[1], pos[2]);
        std::fflush(stdout);
        return true;
    }

    if (DoorAfterMergeForTest(1, pos)) { // coop-pt1111j
        return true;
    }

    static const char *const spec = TestEnv("DC_WORLDS_TEST_GATE_WARP");
    static long long         at = -1;
    static float             x = 0.0f, z = 0.0f, y = 20.0f;
    static bool              parsed = false, spec_ok = true, fired = false;
    static float &           yaw = g_gate_yaw;
    static const int         apart_every = TestEnvInt("DC_WORLDS_TEST_GATE_APART", -1);
    // coop-p2toan: DC_WORLDS_TEST_GATE_APART_ON_BUSY=N: the apart walk-on once, as the other copy's player is heard in his Nth
    // talk indoors (l-local-play -HisBusy talk: she comes in while that talk lasts, whatever her copy's own clock).
    static const int         on_busy = TestEnvInt("DC_WORLDS_TEST_GATE_APART_ON_BUSY", 0);
    static bool              busy_fired = false;
    // coop-fx51c (Jep, 2026-11-04: no fixed tick waits): DC_WORLDS_TEST_GATE_APART_ON_PEER_FLOOR=1: the apart walk-on once, as
    // the other copy is first heard playing a dungeon floor (its zone a floor's, past the entrance and the floor's opening scene:
    // w-town-apart -P2Follows, she follows him onto the floor he plays), whatever her copy's own clock.
    static const bool        on_peer_floor = TestEnvOn("DC_WORLDS_TEST_GATE_APART_ON_PEER_FLOOR");
    // DC_WORLDS_TEST_GATE_APART_ON_PEER_IN=1: the same, as the other copy is first heard inside a building of the town (w-flagsync
    // samedoor: he follows her in by her door once she is in).
    static const bool        on_peer_in = TestEnvOn("DC_WORLDS_TEST_GATE_APART_ON_PEER_IN");
    static bool              peer_floor_fired = false;
    static bool              busy_was = false;
    static int               busy_seen = 0;

    if (on_busy > 0 && g_state.peer_busy != busy_was) {
        busy_was = g_state.peer_busy;
        busy_seen += busy_was ? 1 : 0;
    }
    static int               apart_walked = 0;
    static long long         lockstep_after = 0; // coop-fx51e: "lockstep+K"'s K

    if (!parsed) {
        parsed = true;
        // N:X:Z[:Y[:YAW]]: Y the height it drops from (20 by default; above a raised doorstep), YAW his facing.
        // coop-steamsplit: N "stand" (at -2): on the walk step after the link's state set placed Toan (the resync's
        // landing, "Toan placed where he stood"), an event both copies reach on one tick, instead of a link tick.
        // coop-fx51c (Jep, 2026-11-04: no fixed tick waits): N "loaded" (at -3): once a state file (a row's fixture) is put
        // back, the same tick on both linked copies (NetplayStateFileLoaded).
        if (spec != nullptr && std::strncmp(spec, "stand:", 6) == 0) {
            at = -2;
            spec_ok = std::sscanf(spec + 6, "%f:%f:%f:%f", &x, &z, &y, &yaw) >= 2;
        } else if (spec != nullptr && std::strncmp(spec, "loaded:", 7) == 0) {
            at = -3;
            spec_ok = std::sscanf(spec + 7, "%f:%f:%f:%f", &x, &z, &y, &yaw) >= 2;
        } else if (spec != nullptr && std::strncmp(spec, "never:", 6) == 0) {
            // coop-fx51c: N "never" (at -5): no warp together, the spot alone (the apart walk-on's, DC_WORLDS_TEST_GATE_APART)
            at = -5;
            spec_ok = std::sscanf(spec + 6, "%f:%f:%f:%f", &x, &z, &y, &yaw) >= 2;
        } else if (spec != nullptr && std::strncmp(spec, "lockstep", 8) == 0 && (spec[8] == ':' || spec[8] == '+')) {
            // coop-fx51e (l-local-fixture: a Local join has no state file): N "lockstep" or "lockstep+K" (at -4): K ticks after
            // the tick a catch-up's lockstep began, the same tick on both linked copies (NetplayTestLockstepFrom).
            const char *rest = spec + 8;
            char       *end = nullptr;

            at = -4;
            lockstep_after = rest[0] == '+' ? std::strtoll(rest + 1, &end, 10) : 0;
            rest = end != nullptr ? end : rest;
            spec_ok = rest[0] == ':' && std::sscanf(rest + 1, "%f:%f:%f:%f", &x, &z, &y, &yaw) >= 2;
        } else if (spec == nullptr || std::sscanf(spec, "%lld:%f:%f:%f:%f", &at, &x, &z, &y, &yaw) < 3) {
            spec_ok = false;
        }
    }

    if (!spec_ok || !On()) {
        return false;
    }

    bool now = false;

    // DC_WORLDS_TEST_GATE_AGAIN=1: once apart, the walk-on is armed again for the next time together (after a merge, the
    // link's ticks start over), so a test splits, merges and splits again.
    static const bool again = TestEnvInt("DC_WORLDS_TEST_GATE_AGAIN", 0) != 0;
    // coop-mergesafe: DC_WORLDS_TEST_GATE_AGAIN=N (N > 1): the armed-again walk-on waits N link ticks after the merge (the
    // link's tick goes on from the split's, so it would otherwise come on the merge's own tick).
    static const int  again_after = TestEnvInt("DC_WORLDS_TEST_GATE_AGAIN", 0);
    static long long  again_from = -1;

    if (again && g_state.apart) {
        fired = false;
        again_from = again_after > 1 ? 0 : -1;
    } else if (again_from == 0 && g_state.up) {
        again_from = g_state.tick + again_after;
    }

    if (again_from > 0 && g_state.tick < again_from) {
        return false;
    }

    // coop-mergesafe: DC_WORLDS_TEST_GATE_APART_ROUND=R: the apart walk-on only from this copy's Rth time apart (she stays in
    // town the first time and follows him in the second).
    static const int apart_round_from = TestEnvInt("DC_WORLDS_TEST_GATE_APART_ROUND", 1);
    static int       apart_round = 0;
    static bool      was_apart = false;

    if (g_state.apart && !was_apart) {
        apart_round++;
    }
    was_apart = g_state.apart;
    // coop-herexit2: DC_WORLDS_TEST_GATE_APART_ROUND_TO=R: and only up to this copy's Rth time apart (he walks onto her door
    // once; out again after the merge inside, he stays outside).
    static const int apart_round_to = TestEnvInt("DC_WORLDS_TEST_GATE_APART_ROUND_TO", 0);
    // DC_WORLDS_TEST_GATE_APART_ONCE=1: the apart walk-on once in the whole run.
    static const bool apart_once = TestEnvOn("DC_WORLDS_TEST_GATE_APART_ONCE");
    static bool       apart_fired = false;

    if (apart_round < apart_round_from || (apart_round_to > 0 && apart_round > apart_round_to) || (apart_once && apart_fired)) {
        apart_walked = 0;
    }

    // coop-mergebags: DC_WORLDS_TEST_GATE_SOLO=1: player 1's copy before anyone has joined (no link yet), on its own Nth
    // tick of town walking (Jep's 2026-10-25: he took the mayor's key and left the house before player 2 joined).
    static const bool solo = TestEnvOn("DC_WORLDS_TEST_GATE_SOLO");
    static long long  solo_ticks = 0;

    if (!fired && solo && at >= 0 && Player() == 1 && !g_state.up && !g_state.apart && ++solo_ticks >= at) {
        fired = now = true;
        std::fprintf(stderr, "worlds: test: Toan to (%.0f %.0f) alone (no link yet), tick %lld\n", x, z, solo_ticks);
    } else if (!fired &&
               (at >= 0     ? g_state.tick >= at
                : at == -2  ? g_stand_placed
                : at == -3  ? NetplayStateFileLoaded()
                            : at == -4 && NetplayTestLockstepFrom() >= 0 && g_state.tick >= NetplayTestLockstepFrom() + lockstep_after) &&
               g_state.up && !g_state.apart && !NetplayStateLoadPending()) { // (coop-cavedoorfix: after a state file's load)
        // Linked and together: on link tick N, both copies (the same tick in each).
        fired = now = true;
        std::fprintf(stderr, "worlds: test: Toan to (%.0f %.0f) at link tick %lld\n", x, z, (long long) g_state.tick);
        NetplayTestInputMark("warp"); // coop-p2toan: a held --input script goes on (DC_TEST_INPUT_HOLD; l-local-play -HisBusy talk)

        // coop-flagsync: DC_WORLDS_TEST_FLOOR_REACHED=D:N (both copies, this same linked tick): dungeon D's deepest floor
        // reached set to N, as a save that has been no deeper, so a floor the test then plays is a first visit.
        static const char *const reached = TestEnv("DC_WORLDS_TEST_FLOOR_REACHED");
        int                      dungeon = -1, floor = -1;

        // In town the dungeon status is the save's (UserStatus is the dungeon's own, unset here).
        CUserStatus *status = SaveData != nullptr && SaveData->GetDngStatus() != nullptr ? (CUserStatus *) SaveData->GetDngStatus() : UserStatus;

        if (reached != nullptr && std::sscanf(reached, "%d:%d", &dungeon, &floor) == 2 && dungeon >= 0 && dungeon < 7 && status != nullptr) {
            std::fprintf(stderr, "worlds: test: dungeon %d's deepest floor reached %d -> %d\n", dungeon, (int) status->floor_reached[dungeon], floor);
            status->floor_reached[dungeon] = (s8) floor;
            if (UserStatus != nullptr && UserStatus != status) {
                UserStatus->floor_reached[dungeon] = (s8) floor;
            }
        }
    } else if (apart_every > 0 && g_state.apart && (on_busy > 0 ? busy_seen >= on_busy && (g_state.peer_busy || g_state.peer_talk) && !busy_fired
                       : on_peer_floor ? !peer_floor_fired && g_state.peer_heard && g_state.peer.mode == 3 && g_state.peer.floor >= 0
                       : on_peer_in    ? !peer_floor_fired && g_state.peer_heard && g_state.peer.mode == GAME_MODE_EDIT && g_state.peer.floor > 0
                                       : ++apart_walked >= apart_every)) {
        busy_fired = on_busy > 0; // coop-p2toan: once, on his Nth talk (DC_WORLDS_TEST_GATE_APART_ON_BUSY)
        peer_floor_fired = on_peer_floor || on_peer_in;
        // DC_WORLDS_TEST_GATE_APART=K: the copy left in town walks onto the entrance itself every K ticks of its own
        // town walking (a pick of the town on the world map brings it back to try again).
        apart_walked = 0;
        now = true;
        // coop-localfix: DC_WORLDS_TEST_GATE_APART_SPOT=X:Z: the apart walk-on's spot, when it is not the together warp's
        // (w-town-apart -P1Busy cutscene: he at the mayor's door together, she onto the town's entrance apart).
        // coop-dunfreeze: X:Z:YAW:Y, the facing and the drop height too (w-town-cavereach herevent: she faces the cave door,
        // from 20, not the mayor's raised doorstep's 80 of the together warp, which set her on the ledge above it).
        if (const char *spot = TestEnv("DC_WORLDS_TEST_GATE_APART_SPOT")) {
            std::sscanf(spot, "%f:%f:%f:%f", &x, &z, &yaw, &y);
        }
        apart_fired = true;
        std::fprintf(stderr, "worlds: test: this copy (apart, player %d) puts Toan on the entrance\n", Player());
    }

    if (now) {
        // The cave on the entrance's world map as in a save that has been there (Jep's): its dungeon entered.
        if (SaveData != nullptr && SaveData->QuestDungeon(0, 0) == 0) {
            SaveData->QuestDungeon(0, 1);
            std::fprintf(stderr, "worlds: test: Divine Beast Cave marked entered (the world map shows it)\n");
        }

        TestGameFlag();

        pos[0] = x;
        pos[1] = y;
        pos[2] = z;
        pos[3] = 1.0f;
        std::fflush(stderr);
    }

    return now;
}

bool CoopWorldsTestPartnerWarp(float *pos) {
    // coop-warmconvert (WS7 round 4, t-p2-house-cam as warm jobs): a job switch, once a job (its "loaded" on the job's own
    // fixture load, NetplayJobStateFileLoaded). DC_COOP_MUTATE=partnerwarp_job_kept (warm-repeat-selftest -Group mutations-misc
    // -CoopTwin partnerwarp_job_kept): once a process, as before.
    if (DoorAfterMergeForTest(2, pos)) { // coop-pt1111j
        return true;
    }

    COOP_TEST_SWITCH(const char *, spec, TestEnv("DC_WORLDS_TEST_PARTNER_WARP"));
    static long long at = -1;
    static float     x = 0.0f, z = 0.0f, y = 20.0f, yaw = 0.0f;
    static bool      parsed = false, fired = false;
    static int       job = TestJobGeneration();

    if (job != TestJobGeneration()) {
        job = TestJobGeneration();
        if (!TestJobResetKept("partnerwarp_job_kept")) {
            parsed = fired = false;
            y = 20.0f;
            yaw = 0.0f;
        }
    }

    // Test hook (DC_WORLDS_TEST_PARTNER_WARP=N:X:Z): at link tick N, linked and together, player 2's partner put at
    // (X, Z) on both copies (the cave marked entered, so the entrance's world map offers it).
    if (!parsed) {
        parsed = true;
        // N:X:Z[:Y[:YAW]]: the height she drops from and her facing. coop-fx51c: N "loaded" (at -3), as the gate warp's.
        if (spec != nullptr && std::strncmp(spec, "loaded:", 7) == 0) {
            at = std::sscanf(spec + 7, "%f:%f:%f:%f", &x, &z, &y, &yaw) >= 2 ? -3 : -1;
        } else if (spec != nullptr && std::strncmp(spec, "shown:", 6) == 0) {
            // coop-townstand: N "shown" (at -4): the first tick she is shown in the town outdoors with no town event running
            // (a debug start's arrival over and her join placed), the event a maker's pair waits on instead of a tick.
            at = std::sscanf(spec + 6, "%f:%f:%f:%f", &x, &z, &y, &yaw) >= 2 ? -4 : -1;
        } else if (spec == nullptr || std::sscanf(spec, "%lld:%f:%f:%f:%f", &at, &x, &z, &y, &yaw) < 3) {
            at = -1;
        }
    }

    const bool shown = at == -4 && mode == GAME_MODE_EDIT && EdInteriorFlag == 0 && GameMode != ED_MODE_EVENT && coop::TownPartnerShown(nullptr, nullptr);

    if (at == -1 || fired || !On() || !g_state.up || g_state.apart ||
        (at == -3 ? !NetplayJobStateFileLoaded() || NetplayStateLoadPending() : at == -4 ? !shown : g_state.tick < at)) {
        return false;
    }

    fired = true;

    if (SaveData != nullptr && SaveData->QuestDungeon(0, 0) == 0) {
        SaveData->QuestDungeon(0, 1);
    }

    TestGameFlag();

    pos[0] = x;
    pos[1] = y;
    pos[2] = z;
    pos[3] = yaw;
    std::fprintf(stderr, "worlds: test: player 2's partner to (%.0f %.0f) at link tick %lld\n", x, z, (long long) g_state.tick);
    std::fflush(stderr);
    return true;
}

// The door into a building (ps2/src/editloop.cpp, declared there only).
namespace dc::game {
int GotoInterior(char *name, int entrance, int direction, ED_EVENT_PARAM *param, int start_event);
} // namespace dc::game

int CoopWorldsReenterBuilding() {
    // coop-pt1109e: DC_WORLDS_TEST_HIS_ROOM_AFTER=K (with DC_WORLDS_TEST_HER_ROOM): player 1's copy, apart and walking in town, K ticks after it hears her in the room, in by that room's door:
    // he walks in on her scene. A test's way in: never gated.
    if (static const int after = TestEnvInt("DC_WORLDS_TEST_HIS_ROOM_AFTER", 0); after > 0 && Player() == 1 && g_state.apart && mode == GAME_MODE_EDIT && GameMode == ED_MODE_WALK &&
                                                                                 EdInteriorFlag == 0 && g_state.peer.floor > 0 && g_state.peer_heard) {
        static int  ticks = 0;
        static bool done = false;

        if (const char *const room = TestEnv("DC_WORLDS_TEST_HER_ROOM"); !done && room != nullptr && ++ticks >= after) {
            done = true;
            doorgate::Scan();
            const int row = doorgate::Find(room);
            char      name[32];

            std::snprintf(name, sizeof name, "%s", room);
            std::fprintf(stderr, "worlds: test: he goes in by her door into %s at his apart tick %d\n", room, ticks);
            std::fflush(stderr);
            doorgate::g_mod_door = true;
            GotoInterior(name, row >= 0 && doorgate::g_rooms[row].part_count > 0 ? doorgate::g_rooms[row].parts[0] : 0, 0, nullptr, -1);
            doorgate::g_mod_door = false;
            return 0;
        }
    }
    // coop-pt1109a test hook (DC_WORLDS_TEST_HIS_DOOR_MERGED=X:Z:Y:YAW, x-town-stayermenu -WhoDoor): 130 link ticks after the merge (a compare first), both copies, Toan
    // is put once at a door; his own Cross then takes it.
    if (static const char *const door_spot = TestEnv("DC_WORLDS_TEST_HIS_DOOR_MERGED"); door_spot != nullptr && On() && g_state.up && !g_state.apart && g_state.merges > 0 &&
                                                                                       mode == GAME_MODE_EDIT && EdInteriorFlag == 0 && Chara != nullptr && g_state.tick == g_state.merge_tick + 130) {
        float x = 0.0f, z = 0.0f, y = 0.0f, yaw = 0.0f;

        if (std::sscanf(door_spot, "%f:%f:%f:%f", &x, &z, &y, &yaw) == 4) {
            float at[4] = {x, y, z, 1.0f};
            float rotation[4] = {0.0f, yaw, 0.0f, 0.0f};

            Chara->SetPosition(at);
            Chara->SetRotation(rotation);
            std::fprintf(stderr, "worlds: test: Toan is put at the door (%.0f %.0f) at link tick %lld\n", x, z, (long long) g_state.tick);
        }
    }

    // coop-housefix (tests): DC_COOP_TEST_ENTER=T:NAME:ENTRANCE:EVENT takes both copies of a linked pair into a building
    // together at link tick T, walking in their town, the way a door call's yes does (g_door_yes_building, below), so a state
    // fixture taken in that building (DC_STATE_LOAD_AT) loads there without the walk. Unlinked, or apart, it goes in alone
    // by the door (GotoInterior). ENTRANCE and EVENT (-1: none) are the door's.
    if (static const char *const enter = TestEnv("DC_COOP_TEST_ENTER"); enter != nullptr) {
        static bool        done = false;
        static long long   at = -1;
        static char        name[32] = {};
        static int         entrance = -1, event = -1;

        if (at < 0 && std::sscanf(enter, "%lld:%31[^:]:%d:%d", &at, name, &entrance, &event) != 4) {
            at = 0x7fffffffffffffffLL;
        }
        if (!done && NetplayLinkUp() && NetplayLinkTick() >= at && mode == GAME_MODE_EDIT && GameMode == ED_MODE_WALK && EdInteriorFlag == 0) {
            done = true;
            char said[96];
            std::snprintf(said, sizeof said, "test: in by the door of %s at link tick %lld (DC_COOP_TEST_ENTER)", name, (long long) NetplayLinkTick());
            Log(said);
            TestGameFlag(); // (DC_WORLDS_TEST_GAME_FLAG, as with a test's warp: 0 is the mayor's first-visit scene, seen)
            if (On() && g_state.up && !g_state.apart) {
                g_door_yes_building = InteriorId(name); // both in, together, below
            } else {
                doorgate::g_mod_door = true; // (a test's way in: never gated)
                GotoInterior(name, entrance, 0, nullptr, event);
                doorgate::g_mod_door = false;
                return 0;
            }
        }
    }
    // coop-housemerge: a joiner catching up to the host's building-start checkpoint goes in by the host's door, apart.
    char name[32];
    int  entrance = 0, jump = 0, start_event = -1;

    if (On() && mode == GAME_MODE_EDIT && GameMode == ED_MODE_WALK && EdInteriorFlag == 0 &&
        NetplayCatchupBuildingDoor(name, sizeof name, &entrance, &jump, &start_event)) {
        Log("catch-up: in by the host's door to its building's start");
        doorgate::g_mod_door = true; // coop-doorgate: never gated
        GotoInterior(name, entrance, 0, nullptr, start_event);
        doorgate::g_mod_door = false;
        EdInteriorJumpID = jump;
        return 0;
    }

    // coop-gatepopup: a door call's yes (doorgate::DoorYes), both copies on the same tick: both go into its house, as below. Dropped
    // if the pair splits or leaves the town first.
    if (g_door_yes_building != 0 && (!On() || !g_state.up || g_state.apart || mode != GAME_MODE_EDIT)) {
        g_door_yes_building = 0;
        Log("doorgate: the yes's way in is dropped (the pair split or left the town)");
    }

    // The first walk after a merge inside a building (both copies, the same tick: the merge's load made them alike).
    int &id = g_door_yes_building != 0 ? g_door_yes_building : g_reenter_building;

    if (id == 0 || !On() || !g_state.up || g_state.apart || mode != GAME_MODE_EDIT || GameMode != ED_MODE_WALK || EdInteriorFlag != 0) {
        return 0;
    }

    const int building = id;

    if (&id == &g_door_yes_building) {
        Log("doorgate: both go in by the call's yes");
    }
    id = 0;
    g_reentering = true;
    return building;
}

int CoopWorldsInteriorId(const char *name) {
    return InteriorId(name);
}

void CoopWorldsResyncReenter(int building, const char *name) {
    if (!On()) {
        return;
    }
    char line[160];

    g_reenter_building = building;
    std::snprintf(line, sizeof line, "the resync's load is the town's: both go back into building %s (%d) on their first walk", name, building);
    Log(line);
}

bool CoopWorldsTestLeaveBuilding() {
    static const int after = TestEnvInt("DC_WORLDS_TEST_LEAVE_BUILDING", -1);
    static int       inside = 0;

    // Test hook (DC_WORLDS_TEST_LEAVE_BUILDING=K): an apart copy inside a building leaves it (as its door does)
    // after K ticks there, each time it is inside apart. DC_WORLDS_TEST_LEAVE_LINKED=1 (both copies): linked copies inside
    // together leave too, on the same tick (the tick count is simulation), as player 1 walking out of the door.
    static const bool linked = TestEnvOn("DC_WORLDS_TEST_LEAVE_LINKED");
    // DC_WORLDS_TEST_LEAVE_TOGETHER_AT=T (both copies): linked and together inside, they leave T link ticks after
    // the first tick together there, once each time together, so both copies leave on the same tick whenever each came
    // in (o-opening-resync; LEAVE_BUILDING's count starts when each copy comes in, so a merge inside parted them).
    static const int  together_at = TestEnvInt("DC_WORLDS_TEST_LEAVE_TOGETHER_AT", -1);
    static bool       together_left = false;
    static long long  together_from = -1;

    if (together_at >= 0 && On()) {
        if (g_state.apart || !g_state.up) {
            together_left = false;
            together_from = -1;
        } else if (together_from < 0) {
            together_from = g_state.tick;   // the first tick together inside (the merge's lockstep tick)
        } else if (!together_left && g_state.tick - together_from >= together_at) {
            together_left = true;
            std::fprintf(stderr, "worlds: test: together inside, player %d's copy leaves the building at link tick %lld\n", Player(),
                         (long long) g_state.tick);
            std::fflush(stderr);
            return true;
        }
    }

    // coop-herexit2 DC_WORLDS_TEST_LEAVE_BY=P:T:K (both copies): linked and together inside, player P takes the door T
    // link ticks after the first tick together there (CoopWorldsLeaveBuildingBy: P goes out, the other stays inside, as
    // P's Cross at the door does); the one that stayed goes out by its own door K of its ticks later, once.
    static int       by_player = 0, by_after = -1, by_then = -1;
    static bool      by_read = false, by_split = false, by_done = false;
    static long long by_from = -1;
    static int       by_inside = 0;

    if (!by_read) {
        by_read = true;
        if (const char *v = std::getenv("DC_WORLDS_TEST_LEAVE_BY"); v != nullptr && std::sscanf(v, "%d:%d:%d", &by_player, &by_after, &by_then) != 3) {
            by_player = 0;
        }
    }
    if (by_player > 0 && On() && !by_done) {
        if (!by_split && g_state.up && !g_state.apart) {
            if (by_from < 0) {
                by_from = g_state.tick;
            } else if (g_state.tick - by_from >= by_after) {
                by_split = true;
                std::fprintf(stderr, "worlds: test: together inside, player %d takes the door at link tick %lld\n", by_player, (long long) g_state.tick);
                std::fflush(stderr);
                if (CoopWorldsLeaveBuildingBy(by_player)) {
                    by_done = true; // (this copy is the one going out)
                    return true;
                }
            }
        } else if (by_split && g_state.apart && ++by_inside >= by_then) {
            by_done = true;
            std::fprintf(stderr, "worlds: test: the one that stayed (player %d) goes out of the building %d ticks after the split\n", Player(), by_inside);
            std::fflush(stderr);
            NetplayTestSeriesMark();
            return true;
        }
    }

    // DC_WORLDS_TEST_GATE_SOLO=1 too: player 1's copy alone, before any join.
    static const bool solo = TestEnvOn("DC_WORLDS_TEST_GATE_SOLO");

    if (after < 0 || !On() || !(g_state.apart || (linked && g_state.up) || (solo && Player() == 1 && !g_state.up && !g_state.apart))) {
        inside = 0;
        return false;
    }

    if (++inside < after) {
        return false;
    }

    inside = 0;
    std::fprintf(stderr, "worlds: test: this copy (apart, player %d) leaves the building\n", Player());
    std::fflush(stderr);
    return true;
}

// The map's select state each frame while the test's cursor is held (DC_WORLDS_TEST_MAP_CURSOR): after half a second
// on it, the pick confirmed as Cross would (a test's Cross presses can miss a late-opening map).
static int g_map_held = 0;

int CoopWorldsTestMapPick() {
    static const int place = TestEnvInt("DC_WORLDS_TEST_MAP_CURSOR", -1);

    // (coop-pt1109f: a stand-in story scene: the pick waits for the watcher, or it leaves before anyone joined)
    if (DoorAsSceneForTest() && g_state.apart) {
        return 0;
    }
    if (place < 0 || ++g_map_held < 30) {
        return 0;
    }

    g_map_held = 0;
    std::fprintf(stderr, "worlds: test: the world map's pick confirmed\n");
    std::fflush(stderr);
    return 1;
}

void CoopWorldsTestMapCursor(int *cursor) {
    static const int place = TestEnvInt("DC_WORLDS_TEST_MAP_CURSOR", -1);

    // Test hook (DC_WORLDS_TEST_MAP_CURSOR=N): the world map's cursor held on place N (1: Divine Beast Cave), so a
    // test's Cross picks it without walking the cursor there.
    if (place >= 0 && cursor != nullptr && *cursor != place) {
        std::fprintf(stderr, "worlds: test: the world map's cursor to place %d (was %d)\n", place, *cursor);
        std::fflush(stderr);
        *cursor = place;
    }
}

void CoopWorldsCheckTownLeave(int from_mode, int next_map) {
    if (from_mode != GAME_MODE_EDIT || next_map < 200 || next_map >= 300 || !On() || !g_state.up || NetplayLocalPlayer() == 0 ||
        g_state.apart) {
        return;
    }

    std::fprintf(stderr, "worlds: UNSPLIT town exit: this copy (player %d) leaves town for map %d with the pair together\n", NetplayLocalPlayer(),
                 next_map);
    std::fflush(stderr);
}

int CoopWorldsPeerFloorIn(int map) {
    if (!On() || !g_state.apart || !g_state.peer_heard || g_state.peer.mode != GAME_MODE_DUNGEON || g_state.peer.map != map) {
        return -1;
    }
    return g_state.peer.floor;
}

void CoopWorldsGateOpened(int dungeon, int floor) {
    QueueProgress(kWhatGate, floor, 0, dungeon, 0);
}

void CoopWorldsAtlaListMake(int dungeon) {
    if (dungeon >= 0 && dungeon < 6) {
        g_atla_rand[dungeon] = PortRandState();
        g_atla_rand_set[dungeon] = true;
    }
    BtAtraListMake(dungeon);
    if (dungeon >= 0 && dungeon < 6 && UserStatus != nullptr && (On() || CoopDebug())) {
        int                 floor1 = 0;
        const std::uint32_t print = AtlaPrint(UserStatus, dungeon, &floor1);

        std::fprintf(stderr, "worlds: atla: dungeon %d's Atla laid out from this copy's random state %08x: print %08x, floor 1 holds %d\n", dungeon,
                     g_atla_rand[dungeon], print, floor1);
        std::fflush(stderr);
    }
}

int CoopWorldsHereFloor() {
    return mode == GAME_MODE_DUNGEON && UserStatus != nullptr ? Here().floor : -1000;
}

// coop-dungeonjoin: this copy's way onto a floor (off the entrance's floor menu, or a gate's next floor) and whether the
// other copy was heard on a floor of this dungeon as it set out (it arrives where the other already plays).
struct FloorArrival {
    int                                   was = -1000; // Here().floor last asked
    bool                                  arriving = false;
    bool                                  setting_out = false; // on the way (-2): decided as it lands
    std::chrono::steady_clock::time_point at{};
    int                                   builds = 0; // coop-cutscenejoin: NetplayFloorBuilds() as it set out
};
FloorArrival g_floor_arrival;

// coop-cutscenejoin: the floor (selectMapNo * 256 + floor) whose join from this copy's own arrival is under way
// (CoopWorldsPickJoining), -1000 none; and where this copy's player stood as each floor started (its entrance).
int g_pick_join_key = -1000;
struct FloorEntrance {
    int   key = -1000;
    float pos[3] = {};
    float yaw = 0.0f;
} g_floor_entrance;

// coop-cutscenejoin (Jep's 2026-10-27 playtest: "P2 entered the dungeon while P1 was in the intro dungeon cutscene and got
// a separate instance of the floor"): the other copy is on its way onto floor `floor` of this dungeon, that floor built
// (its build's random state heard) and not started yet. Its opening scene plays there: the floor's "go dungeon" comes after
// the scene, so until then the other reports the way (-2), not the floor. DC_WORLDS_MUTATE=cutscene_no_wait (twin): not
// told apart from the way, as before (this copy builds its own floor and plays it alone through the other's scene).
bool PeerOpensFloor(int floor) {
    return On() && g_state.apart && g_state.peer_heard && g_state.peer.mode == GAME_MODE_DUNGEON && g_state.peer.map == selectMapNo &&
           g_state.peer.floor == -2 && g_state.peer_floor_rand_set && g_state.peer_floor_rand_map == selectMapNo &&
           g_state.peer_floor_rand_floor == floor && !Mutated("cutscene_no_wait");
}

// coop-therefirst: the floor arrival's record brought up to where this copy is now. It is read by more than this file's
// per-frame callers (CoopWorldsFloorFirst, for the Hello's stayer bit): a reader in the frame this copy landed, before
// CoopWorldsArrivingOnFloor's own call, saw the way's record (not arriving) on the floor (his Hello said he stayed,
// x-floor-join-reverse-start's "stayer bit is now 1" a line after "pick join"). Whoever reads it updates it first.
void FloorArrivalUpdate() {
    const int here = CoopWorldsHereFloor();
    const int peer = selectMapNo >= 0 && mode == GAME_MODE_DUNGEON ? CoopWorldsPeerFloorIn(selectMapNo) : -1;
    FloorArrival &a = g_floor_arrival;

    if (here != a.was) {
        // coop-floorfollow (F10, x-dungeon-descent -Case ahead: he went a floor ahead through a gate, she followed through hers,
        // and the merge went by her state, then OUT OF SYNC and both back at the entrance): decided as it set out, "the other on
        // a floor of this dungeon" held for his copy (she was on floor 1, he was going to floor 2), and her copy, setting out
        // while his still reported the stairs (a floor's loading is some 350 ticks at -2), took itself as there first. Set out
        // onto the way (-2), it is decided as it lands: it arrives if the other is on that very floor then. Off the floor menu
        // straight onto a floor, or a gate straight to the next one, it lands as it sets out. Nothing else decides it (not a
        // knock-out or a menu on the floor it is on). DC_WORLDS_MUTATE=arrive_setout (twin): decided as it sets out, against
        // any floor of the dungeon, as before.
        // coop-cutscenejoin: on the way, it counts as arriving already when the other is on its way onto this floor (its
        // opening scene): it was there first (CoopWorldsHoldFloorStart). A join from its own arrival (CoopWorldsPickJoining)
        // lands it on the other's floor before its floor starts.
        const bool setout = Mutated("arrive_setout");
        const bool onto = here >= 0 && (a.setting_out || a.was == -1 || (a.was >= 0 && here != a.was));
        const bool opens = UserStatus != nullptr && PeerOpensFloor(UserStatus->cur_floor);

        if (On() && g_state.apart && !setout && here == -2) {
            a.setting_out = true;
            a.arriving = opens;
            a.at = std::chrono::steady_clock::now();
            a.builds = NetplayFloorBuilds();
            g_pick_join_key = -1000;
            if (CoopDebug() && opens) {
                std::fprintf(stderr, "worlds: floor arrival: this copy sets out for a floor (was %d), the other in its opening scene of this floor: "
                                     "it arrives where the other plays, after the scene\n", a.was);
            }
        } else if (On() && g_state.apart && !setout && onto) {
            if (!a.setting_out) { // straight onto a floor: it sets out here too
                a.builds = NetplayFloorBuilds();
                g_pick_join_key = -1000;
            }
            a.setting_out = false;
            // coop-b1lesson: a join in the floor's opening scene (CoopWorldsSceneJoining): the one held for it arrives, the
            // one playing it was there first, whichever of them said the floor first.
            const int scene_join = CoopWorldsSceneJoining();

            a.arriving = scene_join != 0 ? scene_join == 2 : peer == here;
            a.at = std::chrono::steady_clock::now();
            if (CoopDebug()) {
                std::fprintf(stderr, "worlds: floor arrival: this copy lands on floor %d (was %d), the other %s\n", here, a.was,
                             a.arriving ? "already on it: it arrives where the other plays" : "not on it: this copy is here first");
            }
        } else if (On() && g_state.apart && setout && (here == -2 || (a.was == -1 && here >= 0))) {
            a.arriving = peer >= 0 || opens;
            a.at = std::chrono::steady_clock::now();
            a.builds = NetplayFloorBuilds();
            g_pick_join_key = -1000;
            if (CoopDebug()) {
                std::fprintf(stderr, "worlds: floor arrival: this copy sets out for a floor (here %d, was %d), the other %s\n", here, a.was,
                             peer >= 0 ? "on a floor of this dungeon: it arrives where the other plays"
                             : opens   ? "in its opening scene of this floor: it arrives where the other plays, after the scene"
                                       : "not on a floor here");
            }
        } else if (here < -2 || here == -1) {
            a.arriving = false;
            a.setting_out = false;
            g_pick_join_key = -1000;
        }
        a.was = here;
    }

    // coop-floorstay (Jep's 2026-11-04 pack 8 playtest: "P1 joined P2 in the dungeon and the dungeon desynced"): she arrived on
    // his floor 5, he left it for the town, she played it on alone for minutes, he came back by the floor menu, and the merge
    // went by his fresh build of the floor: her kills and chests undone, her copy put back at the stairs. Her record still said
    // she arrived where he played, so neither Hello said it stayed and the host's state won. Once the other copy is heard off
    // this floor (another floor, the floor menu, a town; not the way, -2, which is also its opening scene of this floor), this
    // copy is the one here and the next to come arrives where it plays. DC_WORLDS_MUTATE=arrive_kept (twin): the record kept,
    // as before.
    if (On() && g_state.apart && g_state.peer_heard && a.arriving && !a.setting_out && here >= 0 && here == a.was) {
        const int peer_floor = selectMapNo >= 0 ? CoopWorldsPeerFloorIn(selectMapNo) : -1;

        if (peer_floor != here && peer_floor != -2 && !Mutated("arrive_kept")) {
            a.arriving = false;
            std::fprintf(stderr, "worlds: floor arrival: the other copy left floor %d (now %d): this copy is the one here\n", here, peer_floor);
        }
    }
}

bool CoopWorldsArrivingOnFloor(int within_ms) {
    // Test hook (DC_WORLDS_TEST_HOLD_ARRIVAL=1; coop-therefirst, x-floor-join-reverse-therefirst): apart, landed on a floor
    // from its way (-2), this per-frame call leaves the record as the way left it, so the frame where a reader came before
    // it lasts until the merge's link (Begin inside the old window, every run): only a reader that brings the record up to
    // date itself sees the arrival.
    static const bool hold = TestEnvOn("DC_WORLDS_TEST_HOLD_ARRIVAL");

    if (hold && g_state.apart && CoopWorldsHereFloor() >= 0 && g_floor_arrival.was == -2) {
        static bool said = false;
        if (!said) {
            said = true;
            std::fprintf(stderr, "worlds: test: the floor arrival's record held at the way (here %d) for the per-frame call\n", CoopWorldsHereFloor());
            std::fflush(stderr);
        }
    } else {
        FloorArrivalUpdate();
        // (Armed as this copy sets out: on the fixed build a reader brings the record up to date in the frame it lands, and
        // the held line above never comes.)
        static bool armed = false;
        if (hold && !armed && g_state.apart && g_floor_arrival.setting_out) {
            armed = true;
            std::fprintf(stderr, "worlds: test: the floor arrival's record is held at the way for the per-frame call once this copy lands\n");
            std::fflush(stderr);
        }
    }

    const int here = CoopWorldsHereFloor();
    const int peer = selectMapNo >= 0 && mode == GAME_MODE_DUNGEON ? CoopWorldsPeerFloorIn(selectMapNo) : -1;
    const FloorArrival &a = g_floor_arrival;

    // Arrived on the other's floor, still apart, for at most within_ms. (Not on the way: the stairs down are the game's own
    // loading animation, shown as ever; the hold is its fade's black kept from the floor's first frame on.)
    return a.arriving && On() && g_state.apart && here >= 0 && here == peer &&
           std::chrono::steady_clock::now() - a.at < std::chrono::milliseconds(within_ms);
}

// coop-cutscenejoin (Jep, 2026-10-27: the one there first wins, a join hides inside the arriver's entrance animation, the
// joiner stands at the floor's entrance): this copy arrives on a floor whose opening scene the other copy plays now. Its own
// arrival (the floor's entrance animation, script 100) plays, then its "go dungeon" waits, its window black
// (NetplayArrivalHold), until the other's scene is over and the other is on the floor; then the floor starts and the merge
// by the other's state comes behind the same black. Up to five minutes (the other's scene never ending), or until the other
// leaves the floor's way: then this copy's floor starts alone, as before. Never a hold of the other's: it plays its scene on.
struct FloorStartWait {
    bool                                  on = false;
    std::chrono::steady_clock::time_point since{};
};
FloorStartWait g_floor_start_wait;

bool CoopWorldsHoldFloorStart() {
    constexpr int   kWaitMs = 5 * 60 * 1000;
    FloorStartWait &w = g_floor_start_wait;
    const auto      now = std::chrono::steady_clock::now();
    // coop-b1lesson: and while it joins that scene (CoopWorldsSceneJoining: the other says the floor now, as this copy does),
    // until the other's state is in (from there this copy plays the other's scene, or its floor, in lockstep).
    const bool joins = CoopWorldsSceneJoining() == 2 && g_state.peer_heard && g_state.peer.mode == GAME_MODE_DUNGEON && g_state.peer.map == selectMapNo &&
                       (g_state.peer.floor == UserStatus->cur_floor || g_state.peer.floor == -2);
    const bool      waits = mode == GAME_MODE_DUNGEON && UserStatus != nullptr && g_floor_arrival.arriving && (PeerOpensFloor(UserStatus->cur_floor) || joins) &&
                       !g_merge_state_in;
    char            line[200];

    // (The other's state in: this copy is in the other's scene now, both copies alike; no wait of its own to end.)
    if (w.on && (g_merge_state_in || !g_state.apart)) {
        w.on = false;
        return false;
    }
    if (!w.on) {
        if (!waits) {
            static int said_key = -1000;
            if (CoopDebug() && UserStatus != nullptr && g_state.apart && said_key != selectMapNo * 256 + UserStatus->cur_floor) {
                said_key = selectMapNo * 256 + UserStatus->cur_floor;
                std::fprintf(stderr, "worlds: floor start not held (once a floor): arriving %d setting_out %d opens %d joins %d peer mode %d map %d floor %d rand_set %d rand_map %d rand_floor %d cur %d merge_in %d\n",
                             (int) g_floor_arrival.arriving, (int) g_floor_arrival.setting_out, (int) PeerOpensFloor(UserStatus->cur_floor), (int) joins,
                             g_state.peer.mode, g_state.peer.map, g_state.peer.floor, (int) g_state.peer_floor_rand_set, g_state.peer_floor_rand_map,
                             g_state.peer_floor_rand_floor, (int) UserStatus->cur_floor, (int) g_merge_state_in);
            }
            return false;
        }
        w.on = true;
        w.since = now;
        std::snprintf(line, sizeof line, "floor start held: player %d plays floor %d's opening scene; this copy waits behind its arrival and joins after it",
                      3 - Player(), (int) UserStatus->cur_floor + 1);
        Log(line);
        return true;
    }
    if (waits && now - w.since < std::chrono::milliseconds(kWaitMs)) {
        return true;
    }

    // The other's scene is over (it is on the floor now: the merge comes once this copy's floor plays), or it left, or the
    // wait ran out. The arrival's black counts from here (CoopWorldsArrivingOnFloor's window).
    const bool there = UserStatus != nullptr && g_state.peer_heard && g_state.peer.mode == GAME_MODE_DUNGEON && g_state.peer.map == selectMapNo &&
                       g_state.peer.floor == UserStatus->cur_floor;

    w.on = false;
    g_floor_arrival.at = now;
    std::snprintf(line, sizeof line, "floor start: %s after %lld ms",
                  there   ? "the other's opening scene is over; this copy starts the floor and joins it"
                  : waits ? "the other's scene did not end in time; this floor alone"
                          : "the other left the floor's way; this floor alone",
                  (long long) std::chrono::duration_cast<std::chrono::milliseconds>(now - w.since).count());
    Log(line);
    return false;
}

bool CoopWorldsFloorStartHeld() {
    return g_floor_start_wait.on && g_state.apart && !g_merge_state_in; // coop-b1lesson: not once the other's state is in
}

// coop-b1lesson (Jep's 2026-10-27 playtest: "If P2 starts the B1 tutorial, the screenshots appear to be showing up but
// neither P2 or the old man who talks and the speech bubbles show up"): she came onto B1 while he played its opening scene
// (the elder's lesson, script 199), and her copy, held for it (CoopWorldsHoldFloorStart), showed his camera and his lesson
// board's pictures (coop-cutsceneview, coop-tutimages): the elder, his text and the scene's actors lived on his copy alone.
// Now both copies count as on that floor (Here) from the moment she is held: his copy, playing the scene on the floor it
// built (from the same random state as hers), was there first and sends; his state set is taken mid-scene (SnapshotQuietTick:
// the script VM's state is in the set since coop-scenejoin), lands on her copy where she waits (InCheckpointZone), and the
// rest of the scene plays on both copies in lockstep: the elder, his bubbles, the board, and anything the scene does to
// him happens on her copy too. Latched while apart (the merge's catch-up included), as CoopWorldsPickJoining is.
// DC_WORLDS_MUTATE=lesson_view (twin): she waits and shows his view, as before.
int g_scene_join_key = -1000;
int g_scene_join_role = 0;

// coop-b2join (Jep's 2026-10-27 run: P2 on B2 during P1's scene there saw Toan and the scene's cuts, but not the cat or the
// speech bubbles, then black, then herself invisible): a floor's opening scene is not always a script of its own after the
// arrival, as B1's lesson (199) is. B2's (the cat) plays inside the arrival itself, script 100 (961 ticks on floor 2 against
// 292 for a plain walk-in), so the scene join above took it for a walk-in and she waited on the view. Told apart by what a
// scene has and a walk-in has not: an actor of its own (the cat, _LOAD_CHARA into the event's NPCs, which the script's start
// empties). DC_WORLDS_MUTATE=arrival_scene_view (twin): a walk-in, as before.
// coop-latefall: and a pack of the scene's own among the script's (coop::ArrivalScenePackLoaded): every arrival loads its
// stairwell as an actor too (B1: stair_mos/in01.chr, NPC 1), which made each walk-in a scene. DC_WORLDS_MUTATE=stairs_scene
// (twin): any actor, as coop-b2join had it.
bool CoopWorldsArrivalPlaysScene() {
    if (BtEventInfo.running_script_no != 100 || Mutated("arrival_scene_view") || (!coop::ArrivalScenePackLoaded() && !Mutated("stairs_scene"))) {
        return false;
    }
    for (int i = 0; i < 6; i++) {
        if (NPCUnit[i].frame != nullptr) {
            static int said = -1000;

            if (UserStatus != nullptr && said != selectMapNo * 256 + UserStatus->cur_floor) {
                said = selectMapNo * 256 + UserStatus->cur_floor;
                std::fprintf(stderr, "worlds: floor %d's arrival plays a scene (event actor %d loaded): a scene join\n", (int) UserStatus->cur_floor + 1, i);
            }
            return true;
        }
    }
    return false;
}

int CoopWorldsSceneJoining() {
    if (!On() || !g_state.apart || mode != GAME_MODE_DUNGEON || UserStatus == nullptr) {
        g_scene_join_key = -1000;
        g_scene_join_role = 0;
        return 0;
    }

    const int key = selectMapNo * 256 + UserStatus->cur_floor;

    if (g_scene_join_key == key) {
        return g_scene_join_role;
    }
    g_scene_join_key = -1000;
    g_scene_join_role = 0;

    int built_map = -1, built_floor = -1;

    if (Mutated("lesson_view") || !NetplaySnapshotAvailable() || BtGameModeFlag == BT_GAME_MODE_ENTRANCE_MENU || g_state.floor_hold ||
        g_floor_started_key == key || !g_state.peer_heard || g_state.peer.mode != GAME_MODE_DUNGEON || g_state.peer.map != selectMapNo ||
        !NetplayFloorBuilt(&built_map, &built_floor) || built_map != selectMapNo || built_floor != UserStatus->cur_floor) {
        return 0;
    }

    // Held for the other's opening scene of this floor (it is on its way, -2): she joins it.
    const bool held = g_floor_start_wait.on && g_floor_arrival.arriving && g_state.peer.floor == -2 && PeerOpensFloor(UserStatus->cur_floor);
    // Playing this floor's opening scene on its way onto it, the other (held for it) saying this floor: it sends.
    // (Not its arrival, the floor's entrance animation, script 100: a join during it goes as before, after it, -StartJoin.
    // coop-b2join: unless that arrival plays a scene: ArrivalPlaysScene.)
    const bool plays = !g_floor_arrival.arriving && BtEventMode != 0 && (BtEventInfo.running_script_no != 100 || CoopWorldsArrivalPlaysScene()) &&
                       g_state.peer.floor == UserStatus->cur_floor;

    if (!held && !plays) {
        return 0;
    }
    g_scene_join_key = key;
    g_scene_join_role = held ? 2 : 1;
    g_state.presence_due = std::chrono::steady_clock::now(); // tell the other copy at once
    char line[200];

    std::snprintf(line, sizeof line, held ? "scene join: this copy joins player %d's opening scene of floor %d: his state comes mid-scene"
                                          : "scene join: player %d joins this copy's opening scene of floor %d: its state goes mid-scene",
                  3 - Player(), (int) UserStatus->cur_floor + 1);
    Log(line);
    return g_scene_join_role;
}

bool CoopWorldsPartnerStandsInScene() {
    // DC_WORLDS_MUTATE=lesson_hidden (twin): the partner hidden through the scene, as before.
    const bool stands = On() && !g_state.apart && mode == GAME_MODE_DUNGEON && BtEventMode != 0 && UserStatus != nullptr &&
                        g_floor_started_key != selectMapNo * 256 + UserStatus->cur_floor && !Mutated("lesson_hidden");
    static bool said = false;

    if (stands != said && CoopDebug()) {
        said = stands;
        std::fprintf(stderr, "worlds: the partner %s through the floor's scene (script %d, here %d)\n", stands ? "stands, drawn," : "no longer stands",
                     (int) BtEventInfo.running_script_no, Here().floor);
    }
    return stands;
}

// coop-cutscenejoin (Jep, 2026-10-27: "if the catch up starts very soon after P2 selects the floor P1 is on (or vice
// versa) then with the improved catch up speed it should be done before the entrance animation ends"): this copy, apart,
// picked the floor the other plays, built it since it set out, and its own arrival there (the floor's entrance
// animation, a script under way) has begun: it counts as on that floor from here (Here), so the merge forms now and the
// other's state lands over the arrival (InCheckpointZone), not after it. Latched until the floor starts, the merge, or a
// new way. coop-floorfollow: on the way it is not decided yet (setting_out); the other on this very floor decides it here.
// DC_WORLDS_MUTATE=pick_join_late (twin): from its floor's start, as before.
bool CoopWorldsPickJoining() {
    if (!On() || mode != GAME_MODE_DUNGEON || UserStatus == nullptr) {
        return false;
    }

    const int key = selectMapNo * 256 + UserStatus->cur_floor;

    if (g_pick_join_key == key) {
        return g_state.apart;
    }

    int built_map = -1, built_floor = -1;

    if (!g_state.apart || !(g_floor_arrival.arriving || g_floor_arrival.setting_out) || BtEventMode == 0 || BtGameModeFlag == BT_GAME_MODE_ENTRANCE_MENU || g_state.floor_hold ||
        g_floor_started_key == key || NetplayFloorBuilds() <= g_floor_arrival.builds || !NetplayFloorBuilt(&built_map, &built_floor) ||
        built_floor != UserStatus->cur_floor || !g_state.peer_heard || g_state.peer.mode != GAME_MODE_DUNGEON || g_state.peer.map != selectMapNo ||
        g_state.peer.floor != UserStatus->cur_floor || Mutated("pick_join_late")) {
        return false;
    }
    // coop-b1lesson: not a join in the floor's opening scene (its floor has not started on either copy: it starts after the
    // scene, on both, as the game's own "go dungeon" comes).
    if (CoopWorldsSceneJoining() != 0) {
        return false;
    }

    g_pick_join_key = key;
    g_state.presence_due = std::chrono::steady_clock::now(); // tell the other copy at once
    char line[200];

    std::snprintf(line, sizeof line, "pick join: this copy's own arrival on floor %d is under way with player %d on it: it joins from here",
                  (int) UserStatus->cur_floor + 1, 3 - Player());
    Log(line);
    return true;
}

bool CoopWorldsArrivingApart() {
    return On() && g_state.apart && g_floor_arrival.arriving;
}

void PickJoinMerged() {
    if (g_pick_join_key != -1000 && mode == GAME_MODE_DUNGEON && UserStatus != nullptr) {
        g_floor_started_key = selectMapNo * 256 + UserStatus->cur_floor;
        g_pick_join_key = -1000;
    }
}

int CoopWorldsPeerOpeningFloor() {
    return PeerOpensFloor(g_state.peer_floor_rand_floor) ? g_state.peer_floor_rand_floor : -1;
}

void CoopWorldsArriverStand() {
    const auto &her = g_state.peer_floor_stand;

    // DC_WORLDS_MUTATE=join_beside (twin): beside player 1, as before.
    if (Player() != 1 || mode != GAME_MODE_DUNGEON || UserStatus == nullptr || Mutated("join_beside")) {
        return;
    }

    const int  key = selectMapNo * 256 + UserStatus->cur_floor;
    const bool stood = her.set && her.zone == Here();

    // Her own arrival still under way (a join from it: her presence carries no stand yet): the floor's entrance, where his
    // own arrival ended.
    if (!stood && g_floor_entrance.key != key) {
        return;
    }
    coop::g_partner.arrive_floor = key;
    coop::g_partner.arrive_pos[0] = stood ? her.pos[0] : g_floor_entrance.pos[0];
    coop::g_partner.arrive_pos[1] = stood ? her.pos[1] : g_floor_entrance.pos[1];
    coop::g_partner.arrive_pos[2] = stood ? her.pos[2] : g_floor_entrance.pos[2];
    coop::g_partner.arrive_yaw = stood ? her.yaw : g_floor_entrance.yaw;
    // coop-b1lesson: a join in this copy's floor scene: her arrival ended where the scene stands his player (the floor's
    // entrance, B1's lesson): she stands beside him through it, not inside him. DC_WORLDS_MUTATE=lesson_inside (twin): on him.
    if (CoopWorldsSceneJoining() == 1 && coop::SpawnOccupied(coop::g_partner.arrive_pos, CharaMain.pos) && !Mutated("lesson_inside")) {
        sceVu0FVECTOR rotation;
        float         spot[3];

        CharaMain.GetRotation(rotation);
        coop::SpawnSecondSpot(CharaMain.pos, rotation[1], coop::g_partner.arrive_pos, spot);
        coop::g_partner.arrive_pos[0] = spot[0];
        coop::g_partner.arrive_pos[1] = spot[1];
        coop::g_partner.arrive_pos[2] = spot[2];
    }
    std::fprintf(stderr, "worlds: floor join: player 2 is to stand %s (%.0f %.0f %.0f)\n", stood ? "where she arrived" : "at the floor's entrance",
                 coop::g_partner.arrive_pos[0], coop::g_partner.arrive_pos[1], coop::g_partner.arrive_pos[2]);
    std::fflush(stderr);
}

// coop-dungeonjoin (Jep, 2026-10-27, either order of arrival: the one already on the floor sees nothing): this copy is on a
// floor apart that it did not arrive on where the other already played, and the other is on it now. A merge there goes by
// this copy's state (CoopWorldsSendsMerge), the other catching up behind its black (NetplayArrivalHold).
bool CoopWorldsFloorFirst() {
    if (!On() || !g_state.apart || mode != GAME_MODE_DUNGEON || UserStatus == nullptr || !g_state.peer_heard) {
        return false;
    }

    // coop-therefirst: the arrival as it is now, not as the last per-frame call left it. DC_WORLDS_MUTATE=floor_first_stale
    // (twin): as it was left, as before.
    if (!Mutated("floor_first_stale")) {
        FloorArrivalUpdate();
    }

    const Zone here = Here();

    return here.floor >= 0 && here == g_state.peer && !g_floor_arrival.arriving;
}

extern bool g_entry_hide;

// coop-sceneowner (Jep's 2026-11-04 pack 8 playtest, "still": she played Divine Beast Cave floor 5's opening scene, he answered
// her call and joined it, and he took the scene over): the scene is hers. The one there first plays it, the one who joins
// watches it and gets what she gets. Her floor merge made his member the game's player inside her scene (her member the
// partner), so the scene moved his character, and the game's pad (port 1, the one the scene's text steps on) is player 1's on
// a floor, so his Cross stepped it. In her scene the change waits for the scene's end now: her copy's player plays the rest of
// it on both copies (her member, or the hidden Toan with her member drawn in his place), his copy draws it so, the game's pad
// is hers (NetplayRoute, CoopWorldsSceneOwner) and his pad reaches nothing; at its end, on both copies on one tick
// (CoopWorldsSimTick), her member becomes the partner where the scene left her and his the player where his copy had him.
// In the state set (build\snapshot-allow.txt), so his copy, taking her state mid-scene, knows it. DC_WORLDS_MUTATE=scene_owner_his
// (twin): the change at the merge and port 1 his, as before.
namespace coop {
SceneHandover g_scene_handover = {};
}

// Her member the partner where she stands (at, facing her_yaw), player 1's the game's player at toan (facing yaw).
// In her scene at the merge (coop-floorscene's case: the scene_owner_his twin, or a scene the handover does not cover) the
// scene's task and the player's showing are kept.
static bool FloorRolesSwap(int her, int member, sceVu0FVECTOR at, float her_yaw, sceVu0FVECTOR toan, float yaw, bool in_scene, const char *why,
                           const char *note) {
    // Her member, the game's player until now, becomes the partner where she stands, facing as she faced; her camera goes
    // on from the player's as it is (no cut on her screen).
    coop::g_partner.choice = her;
    if (!coop::Load(her)) {
        std::fprintf(stderr, "worlds: %s: her member %d could not load as the partner\n", why, her);
        return false;
    }
    sceVu0CopyVector(coop::g_partner.pos, at);
    coop::g_partner.velocity[0] = coop::g_partner.velocity[1] = coop::g_partner.velocity[2] = 0.0f;
    coop::g_partner.chara->frame->SetPosition(at);
    coop::g_partner.chara->frame->SetRotation(0.0f, her_yaw, 0.0f);
    coop::g_partner.chara->SetPosition(at);
    coop::g_partner.chara->SetRotation(0.0f, her_yaw, 0.0f);
    coop::PartnerCameraFromPlayer();

    // coop-p1follows (Jep, 2026-10-28: "mirror is preferred", coop-kojoin's the other way round): her member knocked out
    // alone (the game's knockout under way, or its member change up) lies down as the partner, by the co-op down rule; the
    // member change below ends her knockout. DC_WORLDS_MUTATE=ko_partner_up (twin): she stands up and plays on, as before.
    // The partner's floor is this one from here: its new-floor reset (no one arrives lying down) runs now, not on her next
    // step, where it stood her up again.
    const bool knocked_out = UserStatus->hp[her] <= 0 && !Mutated("ko_partner_up");

    coop::NewFloorCheck();
    if (knocked_out) {
        coop::SetLife(0);
        coop::g_partner.down = coop::kDownTicks;
        coop::g_partner.attacking = false;
        coop::PlayMotion(coop::kDownMotion, 2);
    }

    // coop-floorscene (x-floor-herscene: he came onto B1 during her lesson and joined it; the lesson then never moved on, both
    // players' Cross pressed): the character change ends in play (ChangePlayer: GAME_TASK_CHARA_CHANGED), which took her
    // opening scene's script task (GAME_TASK_SCRIPT_RUN) from under it, its text box left waiting for nothing; and it showed
    // the player the scene had hidden. In her scene (the scene join's sender) the scene's task and the player's showing stay
    // as the scene has them.
    const int  scene_task = gameTask;
    const auto scene_visible = BtActStatus.player_visible;

    // The game's player: player 1's member, where his copy has him (already him when hers was the hidden Toan's stand-in).
    if (UserStatus->cur_chara != member && !BecomeMember(member, why)) {
        return false;
    }
    if (in_scene) {
        gameTask = scene_task;
        BtActStatus.player_visible = scene_visible;
        std::fprintf(stderr, "worlds: her floor merge in her floor's opening scene (script %d): its task %d kept\n", (int) BtEventInfo.running_script_no, scene_task);
    }
    sceVu0CopyVector(CharaMain.pos, toan);
    CharaFrame->SetPosition(toan);
    CharaFrame->SetRotation(0.0f, yaw, 0.0f);
    std::fprintf(stderr, "worlds: %s: her member %d is the partner at (%.0f %.0f %.0f), player 1's %d at (%.0f %.0f %.0f)%s%s\n", why, her, at[0], at[1],
                 at[2], member, toan[0], toan[1], toan[2], note, knocked_out ? "; she was knocked out: she lies down" : "");
    std::fflush(stderr);
    return true;
}

int CoopWorldsSceneOwner() {
    return coop::g_scene_handover.due != 0 && On() && !g_state.apart && mode == GAME_MODE_DUNGEON ? 2 : 0;
}

// At her scene's end (a tick of play, the event over), on both copies alike: the change her floor merge held. Split apart in
// it, her copy keeps her member (her floor alone); his copy makes the change (his own floor from here).
static void SceneHandoverTick() {
    auto &h = coop::g_scene_handover;

    if (h.due == 0) {
        return;
    }
    if (!On() || mode != GAME_MODE_DUNGEON || UserStatus == nullptr || CharaFrame == nullptr || (g_state.apart && Player() == 2)) {
        h.due = 0;
        std::fprintf(stderr, "worlds: her scene's handover dropped (%s)\n", mode != GAME_MODE_DUNGEON ? "off the floor" : "apart: her floor alone");
        return;
    }
    if (BtEventMode != 0 || gameTask != GAME_TASK_PLAY) {
        return;
    }

    sceVu0FVECTOR rotation;
    sceVu0FVECTOR at = {CharaMain.pos[0], CharaMain.pos[1], CharaMain.pos[2], 1.0f};
    sceVu0FVECTOR toan = {h.toan[0], h.toan[1], h.toan[2], 1.0f};

    CharaFrame->GetRotation(rotation);
    h.due = 0;
    // (where the scene left her: his place beside her when it is hers now)
    if (coop::SpawnOccupied(toan, at)) {
        float spot[3];

        coop::SpawnSecondSpot(at, rotation[1], toan, spot);
        toan[0] = spot[0];
        toan[1] = spot[1];
        toan[2] = spot[2];
    }
    FloorRolesSwap(h.her, h.member, at, rotation[1], toan, h.yaw, false, "her scene's end", "");
}

bool CoopWorldsFloorBecomePartner() {
    if (Player() != 2 || mode != GAME_MODE_DUNGEON || UserStatus == nullptr || CharaFrame == nullptr) {
        return false;
    }

    // coop-playfix (Jep's 2026-10-31 Local playtest: she started B1's opening scene, he joined it, and he came out as Xiao,
    // she as Toan): through her floor's entrance and its opening scene her copy's game player is still the hidden Toan, her
    // member drawn in his place (g_entry_hide, CoopWorldsHideDungeonPlayer). Her member is hers, not the game's player's:
    // read as Toan, player 1's became Xiao. DC_WORLDS_MUTATE=merge_hidden_toan (twin): the game's player, as before.
    // coop-pt1106-lockup (Jep, 2026-11-06: the player there is not disturbed, the joiner never waits): her menu open as he comes: it goes
    // on as a local one here, before the swap's character change takes its task, so no quiet frame need wait on it.
    coop::FullMenuRetailToLocal(Player());

    const int     mine = coop::TownPlayerMember();
    const bool    hidden_toan = g_entry_hide && UserStatus->cur_chara == CHARA_TOAN && mine > CHARA_TOAN && mine < 6;
    const int     her = hidden_toan && !Mutated("merge_hidden_toan") ? mine : UserStatus->cur_chara;
    const Zone    here = Here();
    sceVu0FVECTOR rotation;
    sceVu0FVECTOR at = {CharaMain.pos[0], CharaMain.pos[1], CharaMain.pos[2], 1.0f};

    CharaFrame->GetRotation(rotation);

    // Player 1's member where his copy has him (his last presence on this floor), else Toan beside her.
    const auto &his = g_state.peer_floor_stand;
    const bool  known = his.set && his.zone == here;
    const int   member = known && his.chara >= 0 && his.chara < 6 && his.chara != her ? his.chara : her == CHARA_TOAN ? CHARA_XIAO : CHARA_TOAN;
    // coop-cutscenejoin: his own arrival still under way (a join from it: no stand heard yet): the floor's entrance.
    const bool    entrance = !known && g_floor_entrance.key == selectMapNo * 256 + UserStatus->cur_floor && !Mutated("join_beside");
    const float  *from = known ? his.pos : entrance ? g_floor_entrance.pos : at;
    sceVu0FVECTOR toan = {from[0], from[1], from[2], 1.0f};
    const float   yaw = known ? his.yaw : entrance ? g_floor_entrance.yaw : rotation[1];

    // On her spot (both came down the stairs and stood): he arrives beside her, she stays where she is on her screen.
    if (coop::SpawnOccupied(toan, at) || (!known && !entrance)) {
        float spot[3];

        coop::SpawnSecondSpot(at, rotation[1], toan, spot);
        toan[0] = spot[0];
        toan[1] = spot[1];
        toan[2] = spot[2];
    }

    // In her floor's opening scene (the scene join's sender). DC_WORLDS_MUTATE=scene_change_task (twin): the change's task over
    // the scene's, as before coop-floorscene (and so no handover: the change at the merge).
    const bool in_scene = CoopWorldsSceneJoining() == 1 && BtEventMode != 0 && !Mutated("scene_change_task");

    if (in_scene && !Mutated("scene_owner_his")) {
        auto &h = coop::g_scene_handover;

        h.due = 1;
        h.her = her;
        h.member = member;
        h.toan[0] = toan[0];
        h.toan[1] = toan[1];
        h.toan[2] = toan[2];
        h.yaw = yaw;
        g_merge_state_in = true; // her copy's merge state is in (his member the player at her scene's end)
        std::fprintf(stderr,
                     "worlds: her floor merge in her floor's opening scene (script %d): the scene stays hers (her pad, her player); at its end her member %d "
                     "becomes the partner and player 1's %d the player at (%.0f %.0f %.0f)\n",
                     (int) BtEventInfo.running_script_no, her, member, toan[0], toan[1], toan[2]);
        std::fflush(stderr);
        return true;
    }
    if (!FloorRolesSwap(her, member, at, rotation[1], toan, yaw, in_scene, "her floor merge", known ? "" : " (his place not heard: beside her)")) {
        return false;
    }
    g_merge_state_in = true; // her copy's player is player 1's from here (and hers the partner)
    return true;
}
bool CoopWorldsFloorJoin() {
    // A floor join needs the snapshot (a build without the fixed layout merges on no floor, as before).
    if (!On() || !g_state.apart || mode != GAME_MODE_DUNGEON || Mutated("no_floor_join") || !NetplaySnapshotAvailable()) {
        return false;
    }

    const Zone here = Here();

    return here.floor >= 0 && g_state.peer_heard && here == g_state.peer;
}

// coop-dungeonp2: each floor this copy arrives on, once it plays: the floor's layout as a sum of where its map events
// stand (chests, gates, Atla), so a test can tell two copies on one floor from two versions of it. coop-floorsnap: the
// snapshot that puts the host's floor here asks for the line again (CoopWorldsFloorLayoutAgain).
int g_layout_floor = -1000;

void CoopWorldsFloorLayoutAgain() {
    g_layout_floor = -1000;
}

void CoopWorldsMergeStateIn() {
    g_merge_state_in = g_state.apart;
}

bool CoopWorldsPeerFloorRand(int floor, std::uint32_t *rand) {
    // DC_WORLDS_MUTATE=own_floor_apart (twin): this copy builds its own floor, as before.
    if (!On() || !g_state.apart || !g_state.peer_heard || !g_state.peer_floor_rand_set || Mutated("own_floor_apart")) {
        return false;
    }

    if (g_state.peer.mode != GAME_MODE_DUNGEON || g_state.peer_floor_rand_map != selectMapNo || g_state.peer_floor_rand_floor != floor) {
        return false;
    }

    *rand = g_state.peer_floor_rand;
    return true;
}

bool CoopWorldsMergeStateInHere() {
    return g_merge_state_in;
}

// coop-p2entry: a floor started since this copy last checked whether its player is player 2's member.
bool g_become_due = false;

// coop-localfix: player 2's copy apart, from its way into a dungeon (or a floor's start) until her member plays there.
bool g_entry_hide = false;

static const char *IdentityName(int chara) {
    static const char *const names[6] = {"Toan", "Xiao", "Goro", "Ruby", "Ungaga", "Osmond"};

    return chara < 0 ? "nobody" : chara < 6 ? names[chara] : "?";
}

// coop-localfix: what player 2's copy apart draws as its own player, logged at each change with how long the last
// lasted (the tests count "as Toan" lines: Jep's rule, she is never Toan unless she picks him).
// coop-localrace: her copy has said, before any link, who it draws (someone, not nobody) and where she stands (a town
// positions line): what the l-local-play rows' pre-link checks read (CoopWorldsPrelinkSaid, the -HerFirst test hook).

bool CoopWorldsPrelinkSaid() {
    return g_prelink_drawn && (g_prelink_spot || !CoopDebug()); // (the spot line is DC_COOP_DEBUG's: without it, the identity alone)
}

void CoopWorldsIdentity(int chara, const char *where) {
    static int         said = -2;
    static const char *said_where = "";
    static long long   frames = 0;

    ++frames;
    if (chara == said && std::strcmp(where, said_where) == 0) {
        return;
    }
    if (said != -2) {
        std::fprintf(stderr, "identity: player 2's copy drew its player as %s (%s) for %lld frames\n",
                     IdentityName(said), said_where, frames);
    }
    said = chara;
    said_where = where;
    frames = 0;
    if (chara >= 0 && !NetplayLinkUp()) {
        g_prelink_drawn = true;
    }
    std::fprintf(stderr, "identity: player 2's copy draws its player as %s (%s, mode %d)\n", IdentityName(chara),
                 where, (int) mode);
    std::fflush(stderr);
}

// coop-localfix (Jep's 2026-10-26 Local playtest, note 3: "P2 entering the dungeon turns into Toan during the entry
// animation, enters as Toan, then turns back"; his rule: player 2 is never Toan unless she picks him on the character
// wheel). A dungeon loads with the game's player, Toan (dun/mainchara/c01d.chr), and her member takes over at the floor's
// first tick of play (arriving, CoopWorldsSimTick). Until then player 2's copy apart draws no player: the entrance's
// scene and the floor's opening show the floor without Toan. From the dungeon's draw (port/src/dun/gameloop.cpp).
// DC_WORLDS_MUTATE=toan_entry (twin): Toan drawn as before.
// coop-joinvisuals: the member her copy draws in the hidden player's place this frame, -1 none (CoopWorldsEntryStandIn).
int g_entry_stand_in = -1;

int CoopWorldsEntryStandIn() {
    return g_entry_stand_in;
}

bool CoopWorldsHideDungeonPlayer() {
    static int played = 0;

    g_entry_stand_in = -1;

    // coop-cutsceneview: showing the other's opening scene (on either copy), the player drawn is the other's (this copy's
    // stands in for it: ScenePoseTick) or none; never this copy's own. (Her identity: nobody of hers.)
    if (CoopWorldsSceneView(nullptr, nullptr, nullptr)) {
        const bool other = CoopWorldsSceneShowsOther();

        if (Player() == 2) {
            CoopWorldsIdentity(-1, other ? "player 1's scene, his player shown" : "player 1's scene");
        }
        return !other;
    }

    // coop-sceneowner: her floor's opening scene, joined, plays on with her copy's player, on both copies: the hidden Toan with
    // her member drawn in his place, as her copy drew it before the merge.
    if (CoopWorldsSceneOwner() == 2 && UserStatus != nullptr) {
        const int  her = coop::g_scene_handover.her;
        const bool hide = UserStatus->cur_chara != her && !Mutated("toan_entry");

        if (hide && her != CHARA_TOAN && her >= 0 && her < 6 && !Mutated("entry_hidden")) {
            g_entry_stand_in = her;
        }
        if (Player() == 2) {
            CoopWorldsIdentity(g_entry_stand_in >= 0 ? g_entry_stand_in : hide ? -1 : UserStatus->cur_chara, "her floor's scene, joined");
        }
        return hide;
    }

    if (!On() || !g_state.apart || Player() != 2 || mode != GAME_MODE_DUNGEON || UserStatus == nullptr || g_merge_state_in) {
        return false;
    }

    const int member = coop::TownPlayerMember();

    // Her member plays (the arriving change ran), or there is none to become, or the change did not happen in half a
    // second of play after it was due (never hidden for good): drawn from here.
    // (coop-twinfix: only on a floor started here: built and its start held behind the other's opening scene, it is not hers yet)
    const bool started_here = g_floor_started_key == selectMapNo * 256 + UserStatus->cur_floor;

    played = !g_become_due && started_here && gameTask == GAME_TASK_PLAY && BtGameModeFlag == BT_GAME_MODE_PLAY ? played + 1 : 0;
    if (UserStatus->cur_chara == member || member == CHARA_TOAN || played >= 30) {
        g_entry_hide = false;
    }

    const bool hide = g_entry_hide && !Mutated("toan_entry");

    // coop-joinvisuals (Jep's 2026-10-27 run: "when P2 enters B1 she is invisible during the entrance animation"): the game's
    // player (Toan) stays hidden and plays the entrance (its scenes play his own motions on him: her model as the player
    // crashed B1's elder scene), and her member is drawn in his place, as he moves and what he plays (coop_party.cpp,
    // EntryStandInDraw). Draw only. DC_WORLDS_MUTATE=entry_hidden (twin): nobody drawn, as before.
    if (hide && member != CHARA_TOAN && member >= 0 && member < 6 && !Mutated("entry_hidden")) {
        g_entry_stand_in = member;
    }
    CoopWorldsIdentity(g_entry_stand_in >= 0 ? g_entry_stand_in : hide ? -1 : UserStatus->cur_chara,
                       g_entry_stand_in >= 0 ? "a floor's entrance, in Toan's place" : "a floor");

    // coop-joinvisuals test hook (DC_WORLDS_TEST_ENTRY_SHOT=PATH:N, player 2's copy apart): her frame N frames into each
    // floor's entrance event, to PATH (once), with what she drew as her player and where the frame before put it.
    if (static const char *const entry_shot = TestEnv("DC_WORLDS_TEST_ENTRY_SHOT"); entry_shot != nullptr) {
        static std::string path;
        static long        at = 0, frames = 0;
        static bool        taken = false;

        if (at == 0) {
            const std::string spec = entry_shot;
            const std::size_t colon = spec.rfind(':');

            path = spec.substr(0, colon);
            at = std::max(1L, std::strtol(spec.c_str() + colon + 1, nullptr, 10));
        }
        frames = BtEventMode != 0 ? frames + 1 : 0;
        if (!taken && frames == at) {
            int spots[8];

            taken = true;
            CoopScreenSpots(spots);
            NetplayTestShotTo(path.c_str());
            std::fprintf(stderr, "worlds: test: entrance frame %ld (event script %d): her player drawn as %s, at feet (%d %d) head (%d %d)\n", frames,
                         (int) BtEventInfo.running_script_no, IdentityName(g_entry_stand_in >= 0 ? g_entry_stand_in : hide ? -1 : UserStatus->cur_chara), spots[0], spots[1], spots[2], spots[3]);

            // coop-entryshadow: the stand-in's body and shadow as posed the frame before (coop_party.cpp, StandInPoseMeasure).
            coop::EntryStandInPoseInfo pose;

            if (coop::EntryStandInPose(&pose)) {
                std::fprintf(stderr, "worlds: test: entrance stand-in pose %d: motion %d time %.1f, body reach %.2f, shadow reach %.2f (%d frames), %d frames matched, gap %.2f\n",
                             pose.frame, pose.motion, pose.time, pose.body_reach, pose.shadow_reach, pose.shadow_frames, pose.matched, pose.gap);
            } else {
                std::fprintf(stderr, "worlds: test: entrance stand-in pose: none posed\n");
            }
        }
    }
    return hide;
}

// coop-twinfix (x-floor-call-yes, nightly 2026-11-01: "identity: player 2's copy draws its player as Toan (a floor)" for 286
// frames; Jep's rule, she is never Toan unless she picks him): a floor built from another floor (her stairs or gate down,
// apart) loads the game's player, Toan, and its start may wait behind the other's opening scene (floor start held), so the
// hide CoopWorldsFloorStarted arms came only after those frames. Armed at the build too, for player 2's copy apart.
// DC_WORLDS_MUTATE=toan_floor_build (twin): from the start only, as before.
void CoopWorldsFloorBuilding() {
    if (On() && g_state.apart && Player() == 2 && !Mutated("toan_floor_build")) {
        g_entry_hide = true;
    }
}

// coop-fx51c test hook (DC_WORLDS_TEST_SLOT_AT_MERGE_WHEN=catchup): this copy's catch-up begins (the host's checkpoint heard, netplay.cpp):
// the test's record, held since the merge's start, goes now (slotsync::TestHoldsToCatch).
void CoopWorldsTestSlotOnCatch() {
    static const bool on = TestEnvIs("DC_WORLDS_TEST_SLOT_AT_MERGE_WHEN", "catchup") && TestEnv("DC_WORLDS_TEST_SLOT_AT_MERGE") != nullptr;

    if (!on || !On() || !g_test_merge_started) {
        return;
    }
    std::fprintf(stderr, "worlds: test: the merge's held record goes as this copy's catch-up begins\n");
    slotsync::SendHeldToCatch();
}

// coop-catchuprace test hook (DC_WORLDS_TEST_SLOT_AT_MERGE_WHEN=lastblob): the host's last blob is here (netplay.cpp): the test's
// record, held since the merge's start, goes now.
void CoopWorldsTestSlotOnLastBlob() {
    static const bool on = TestEnvIs("DC_WORLDS_TEST_SLOT_AT_MERGE_WHEN", "lastblob") && TestEnv("DC_WORLDS_TEST_SLOT_AT_MERGE") != nullptr;

    if (On() && g_state.test_progress_held) { // coop-catchuprace (DC_WORLDS_TEST_FLAG_AT_MERGE)
        g_state.test_progress_held = false;
        g_state.progress_due = std::chrono::steady_clock::now();
        std::fprintf(stderr, "worlds: test: the merge's held progress goes as the host's last blob is here\n");
        std::fflush(stderr);
    }
    if (!on || !On() || !g_test_merge_started) {
        return;
    }
    std::fprintf(stderr, "worlds: test: the merge's held record goes as the host's last blob is here\n");
    slotsync::SendHeldToCatch();
}

void CoopWorldsFloorStarted() {
    g_pick_join_key = -1000; // coop-cutscenejoin: started (Here)
    if (UserStatus != nullptr && CharaFrame != nullptr) {
        sceVu0FVECTOR rotation;

        CharaFrame->GetRotation(rotation);
        g_floor_entrance.key = selectMapNo * 256 + UserStatus->cur_floor;
        g_floor_entrance.pos[0] = CharaMain.pos[0];
        g_floor_entrance.pos[1] = CharaMain.pos[1];
        g_floor_entrance.pos[2] = CharaMain.pos[2];
        g_floor_entrance.yaw = rotation[1];
    }
    g_become_due = true;
    g_floor_played = false;
    g_entry_hide = true;

    if (UserStatus != nullptr) {
        g_floor_started_key = selectMapNo * 256 + UserStatus->cur_floor;
        g_floor_started_prev = UserStatus->prev_floor; // coop-floorunlock
    }
}

// coop-fx51c (Jep, 2026-11-04: every row a headless fixture): a state file put back on a floor (a fixture, DC_STATE_LOAD_AT).
// The file was saved on a quiet tick of the floor's play, its opening over, but this copy's floor start (CoopWorldsFloorStarted,
// which the state set does not hold) is the one its --jump made, if any: Here() said "the entrance" (-2) for the floor the pair
// stands on, and a split or merge on it went by the entrance (w-world-escape-p1-online~merge on dun1-f1-launcher never merged).
// The floor counts as started and played here, as on the copy that saved it. DC_WORLDS_MUTATE=load_floor_unstarted (twin):
// left as the jump made it, as before.
void CoopWorldsStateFileLoaded() {
    // coop-fx51c: the bags and flags as the file put them (DC_WORLDS_TEST_BAGDUMP / _FLAGDUMP), a row's "before" on its fixture
    // (x-door-gate: the story's flag unset before the door, with no 10 s of play before it to dump them)
    slotsync::BagDump("loaded");
    if (mode != GAME_MODE_DUNGEON || UserStatus == nullptr || BtGameModeFlag == BT_GAME_MODE_ENTRANCE_MENU || Mutated("load_floor_unstarted")) {
        return;
    }
    g_floor_started_key = selectMapNo * 256 + UserStatus->cur_floor;
    g_floor_started_prev = UserStatus->prev_floor;
    g_floor_played = true;
    // coop-fx51a (b2, x-fixture-fidelity -Globals 2026-11-05: the loader against the maker): the floor start's own arms, as
    // CoopWorldsFloorStarted set them on the copy that saved the file (where it stood then is in the state set, g_floor_entrance);
    // and the partner mark's log window from here (coop_interact: its 60-frame window, which x-dungeon-mark reads only past
    // clock 85, so its phase decides nothing there). DC_WORLDS_MUTATE=load_entry_unarmed (twin): left as the jump left them.
    if (!Mutated("load_entry_unarmed")) {
        g_become_due = true;
        g_entry_hide = true;
        coop::PartnerMarkLogReset();
    }

    char where[96];

    Describe(Here(), where, sizeof where);
    std::fprintf(stderr, "worlds: the state file puts this copy %s\n", where);
    std::fflush(stderr);
}

// pt1111c test hook (DC_WORLDS_TEST_WALKIN=<who>:<flag>, x-standin-story): once the floor plays, game flag <flag> is cleared
// on this copy and the floor's marker whose script writes it is switched on (as the floor's start does while the story is
// unseen: the cave's floor 8, 25 and its marker 300); who standin: the player becomes the first member who has not joined (as
// a copy staying apart does, BecomeMember), toan: stays Toan; then the player is put on that marker, so retail's walk check
// starts its event (or the guard holds it). It says what the floor gate makes of the floor now (GatherFor).
static void WalkInTest() {
    static const char *const spec = TestEnv("DC_WORLDS_TEST_WALKIN");
    static int               stage = 0;
    static int               want = -1;
    static CDungeonEventData *slot = nullptr;
    char                     who[16] = "";
    int                      flag = -1;

    if (spec == nullptr || stage >= 3 || mode != GAME_MODE_DUNGEON || UserStatus == nullptr || SaveData == nullptr || NowEventMan == nullptr ||
        gameTask != GAME_TASK_PLAY || std::sscanf(spec, "%15[a-z]:%d", who, &flag) != 2) {
        return;
    }

    // who apart (the follow-up's row): this copy waits until it stays on the floor apart as its own member (Toan took the way out
    // on player 1's copy, CoopWorldsStayOnFloor), and is that member, joined or not.
    const bool apart = std::strcmp(who, "apart") == 0;

    if (stage == 0 && apart && !(g_state.apart && UserStatus->cur_chara != CHARA_TOAN)) {
        return;
    }
    if (stage == 0) {
        for (CDungeonEventData &data : NowEventMan->event) {
            int flags[4] = {-1, -1, -1, -1};
            const int sets = data.event != nullptr && data.event->script_no >= 0 && EdEventScript.prog != nullptr
                                 ? std::min(StoryFlags(EdEventScript.prog, data.event->script_no, flags, 4), 4)
                                 : 0;

            for (int i = 0; i < sets && slot == nullptr; i++) {
                slot = flags[i] == flag ? &data : nullptr;
            }
        }
        if (slot == nullptr) {
            std::fprintf(stderr, "worlds: test: walk-in: no marker of floor %d writes game flag %d\n", (int) UserStatus->cur_floor + 1, flag);
            stage = 3;
            return;
        }
        CoopWorldsLocalFlags(true); // (this copy's own: not progress for another copy)
        SaveData->SetGameFlag(flag, 0);
        CoopWorldsLocalFlags(false);
        slot->switch_on = 1;
        const int gather = GatherFor(selectMapNo, UserStatus->cur_floor);
        std::fprintf(stderr, "worlds: test: walk-in: game flag %d cleared, event %d's marker on; the gate makes %s %s (story flag %d)\n", flag,
                     (int) slot->event->script_no, FloorName(selectMapNo, UserStatus->cur_floor),
                     gather == kGatherScene ? "a story floor" : gather == kGatherBoss ? "a boss floor" : "no gathered floor",
                     NetplayFloorStoryFlag(selectMapNo, UserStatus->cur_floor));
        want = apart ? (int) UserStatus->cur_chara : std::strcmp(who, "standin") == 0 ? (int) UserStatus->party_size : CHARA_TOAN;
        if (want != UserStatus->cur_chara) {
            BecomeMember(want, "test: walk-in");
        }
        stage = 1;
        return;
    }

    if (stage == 1 && UserStatus->cur_chara == want) {
        sceVu0FVECTOR to = {slot->pos[0], CharaMain.pos[1], slot->pos[2], 1.0f};

        CharaMain.SetPosition(to); // (as the warp test does, coop_core.cpp)
        sceVu0CopyVector(CharaMain.pos, to);
        std::fprintf(stderr, "worlds: test: walk-in: player %d (chara %d, party of %d) put on event %d's marker at (%.0f %.0f)\n", Player(),
                     (int) UserStatus->cur_chara, (int) UserStatus->party_size, (int) slot->event->script_no, slot->pos[0], slot->pos[2]);
        std::fflush(stderr);
        stage = 3;
    }
}

void CoopWorldsSimTick() {
    WalkInTest(); // pt1111c
    static const std::int64_t exit_at = TestExitAt();

    int       &layout_floor = g_layout_floor;
    const int  floor_id = selectMapNo * 256 + (UserStatus != nullptr ? UserStatus->cur_floor : 0);

    if (CoopDebug() && floor_id != layout_floor && NowDngMap != nullptr && gameTask == GAME_TASK_PLAY) {
        std::uint32_t sum = 2166136261u;

        for (const DUNGEON_EVENT &event : NowDngMap->events) {
            const std::int32_t parts[3] = {event.kind, (std::int32_t) std::lround(event.pos[0]), (std::int32_t) std::lround(event.pos[2])};

            for (std::int32_t part : parts) {
                sum = (sum ^ (std::uint32_t) part) * 16777619u;
            }
        }

        layout_floor = floor_id;
        std::fprintf(stderr, "worlds: floor layout: map %d floor %d sum %08x (player %d, %s)\n", selectMapNo,
                     UserStatus != nullptr ? (int) UserStatus->cur_floor : -1, sum, Player(), g_state.apart ? "apart" : "together");
    }

    static bool               apart_fired = false;

    // DC_WORLDS_TEST_ATLA (the test's): once a second, whether the floor plays (the atla hook counts only then).
    static const int atla_test = TestEnvInt("DC_WORLDS_TEST_ATLA", -1);
    static int       atla_seen = 0;

    if (atla_test >= 0 && On() && g_state.apart && ++atla_seen % 60 == 0) {
        std::fprintf(stderr, "worlds: test: floor task %d, atla ticks %d of %d\n", (int) gameTask, g_state.progress_ticks, atla_test);
    }

    if (!On() || gameTask != GAME_TASK_PLAY) {
        return;
    }

    // DC_WORLDS_TEST_FLAG_AT=T:bit|int:F1/F2/...:V (the test's, coop-floorgate): together, both copies set game flags (int flags)
    // F1, F2 ... to V at the first play tick from link tick T on, the same tick on both (a floor's opening scene unseen, a boss beaten).
    // (coop-twinfix: more than one such set, comma-separated, each with its own kind and value: x-floor-call's floor 5 scene
    // unseen and floor 4's mid-floor story seen, so floor 4 is no gathered floor since coop-b4story)
    static const char *const flag_at = TestEnv("DC_WORLDS_TEST_FLAG_AT");
    static bool              flag_at_done = false;

    if (flag_at != nullptr && !flag_at_done && g_state.up && !g_state.apart && SaveData != nullptr && g_state.tick >= std::atoll(flag_at)) {
        flag_at_done = true;
        for (const char *set = flag_at; set != nullptr && *set != 0; set = std::strchr(set, ',') != nullptr ? std::strchr(set, ',') + 1 : nullptr) {
            long long at = 0;
            char      kind[8] = "";
            char      list[200] = "";
            int       value = 1;

            if (std::sscanf(set, "%lld:%7[a-z]:%199[0-9/]:%d", &at, kind, list, &value) != 4) {
                std::fprintf(stderr, "worlds: test: DC_WORLDS_TEST_FLAG_AT '%s' not understood\n", set);
                continue;
            }
            const bool is_int = std::strcmp(kind, "int") == 0;

            for (const char *f = list; *f != 0; f = std::strchr(f, '/') != nullptr ? std::strchr(f, '/') + 1 : f + std::strlen(f)) {
                if (is_int) {
                    SaveData->SetGameIntFlag(std::atoi(f), value);
                } else {
                    SaveData->SetGameFlag(std::atoi(f), value);
                }
                std::fprintf(stderr, "worlds: test: link tick %lld: game %s flag %d set to %d on both copies\n", (long long) g_state.tick,
                             is_int ? "int" : "bit", std::atoi(f), value);
            }
        }
        std::fflush(stderr);
    }

    StepBesideVote(); // coop-p2prompt: her exit gate's question to player 1, beside the game

    if (gameTask != GAME_TASK_PLAY) {
        return; // both said yes: the gate's script starts
    }

    if (BtEventMode == 0 && mode == GAME_MODE_DUNGEON) {
        g_floor_played = true; // coop-localfix: its opening scene over (MergeReady)
    }
    if (coop::g_scene_handover.due != 0) {
        SceneHandoverTick(); // coop-sceneowner: her scene over, the change her floor merge held
        if (coop::g_scene_handover.due == 0 && gameTask != GAME_TASK_PLAY) {
            return; // the change runs first
        }
    }

    // Player 2 left anyway: its change of member has run, so the gate's script starts, as its Cross at the gate does.
    if (g_gate_again.script >= 0 && g_state.apart) {
        BtEventInfo.script_no = g_gate_again.script;
        BtEventInfo.ext_memory = g_gate_again.ext;
        BtEventInfo.event_marker = true;
        BtEventInfo.action_mode = 1;
        sceVu0CopyVector(BtEventInfo.position, g_gate_again.position);
        sceVu0CopyVector(BtEventInfo.direction, g_gate_again.direction);
        gameTask = GAME_TASK_SCRIPT_START;
        std::fprintf(stderr, "worlds: vote: player 2 goes through the gate (script %d)\n", g_gate_again.script);
        std::fflush(stderr);
        g_gate_again.script = -1;
        return;
    }

    // coop-p2entry: player 2's copy alone on a floor plays her own member, wherever it came from (her way in from town
    // loads the floor with the game's player, Toan: Jep's 2026-10-25 playtest, "P2 joined B1 as Toan instead of Xiao").
    // Once each floor, at its first tick of play, where the floor put the player. DC_WORLDS_MUTATE=no_become (twin): Toan.
    if (g_become_due && g_state.apart && Player() == 2 && UserStatus != nullptr && CharaFrame != nullptr) {
        g_become_due = false;

        const int chara = coop::TownPlayerMember();
        const int was = UserStatus->cur_chara;

        if (chara != was && !Mutated("no_become")) {
            float at[4] = {CharaMain.pos[0], CharaMain.pos[1], CharaMain.pos[2], 1.0f};

            if (BecomeMember(chara, "arriving")) {
                sceVu0CopyVector(CharaMain.pos, at);
                CharaFrame->SetPosition(at);
                std::fprintf(stderr, "worlds: arriving: player 2's copy plays her member %d on floor %d (was %d) at (%.0f %.0f %.0f)\n", chara,
                             (int) UserStatus->cur_floor + 1, was, at[0], at[1], at[2]);
                std::fflush(stderr);
                return; // the change runs first
            }
        } else {
            std::fprintf(stderr, "worlds: arriving: player 2's copy on floor %d as %d (her member %d)%s\n", (int) UserStatus->cur_floor + 1, was,
                         chara, chara != was ? ", left as it was (no_become)" : "");
            std::fflush(stderr);
        }
    }

    // coop-p2entry: DC_COOP_DEBUG, player 2's copy apart on a floor once a second: its player, where it stands, its
    // floor play so far (w-town-apart -Case p2plays).
    if (static int seen = 0; CoopDebug() && g_state.apart && Player() == 2 && UserStatus != nullptr && ++seen % 60 == 0) {
        std::fprintf(stderr, "worlds: apart on a floor: player chara %d at (%.0f %.0f %.0f), floor %d, play ticks %d\n", (int) UserStatus->cur_chara,
                     CharaMain.pos[0], CharaMain.pos[1], CharaMain.pos[2], (int) UserStatus->cur_floor + 1, seen);
    }

    // Test hook (coop-dungeonjoin, DC_WORLDS_TEST_KEEP_LIFE=1): this copy's player, apart on a floor, keeps its full life (a
    // test's long wait there for the other copy, x-floor-join -Seamless: the floor's monsters knocked him out first).
    if (static const bool keep_life = TestEnvOn("DC_WORLDS_TEST_KEEP_LIFE"); keep_life && g_state.apart && mode == GAME_MODE_DUNGEON && UserStatus != nullptr) {
        const int chara = UserStatus->cur_chara;

        UserStatus->hp[chara] = UserStatus->max_hp[chara];
        UserStatus->next_hp[chara] = UserStatus->max_hp[chara]; // (a hit's loss runs down to next_hp: CheckLife reads it)
        UserStatus->life_step[chara] = 0;
    }

    // Test hook (coop-localfix, DC_WORLDS_TEST_KO=K): this copy's player, apart on a floor, is knocked out after K ticks of
    // its floor's play (its life to 0, as a monster's last hit leaves it), once: the knocked-out case of a floor merge.
    if (static const int ko_at = TestEnvInt("DC_WORLDS_TEST_KO", -1); ko_at >= 0 && g_state.apart && mode == GAME_MODE_DUNGEON && UserStatus != nullptr && BtEventMode == 0) {
        static int  ko_ticks = 0;
        static bool ko_done = false;

        if (!ko_done && ++ko_ticks >= ko_at) {
            ko_done = true;
            UserStatus->hp[UserStatus->cur_chara] = 0;
            std::fprintf(stderr, "worlds: test: this copy (apart, player %d) knocked out on floor %d (chara %d)\n", Player(), (int) UserStatus->cur_floor + 1,
                         (int) UserStatus->cur_chara);
        }
    }

    // DC_WORLDS_TEST_ATLA=K: an apart copy collects an atla (part 0, slot 0, as the game's own pickup records
    // it) after K ticks of floor play.
    static const int atla_after = TestEnvInt("DC_WORLDS_TEST_ATLA", -1);

    // coop-apartstats: DC_WORLDS_TEST_ATLA_ID=A, atla A instead (0x28 and on: a Georama chip), its town dumped before and after.
    static const int atla_id = TestEnvInt("DC_WORLDS_TEST_ATLA_ID", 0);

    if (atla_after >= 0 && g_state.apart && SaveData != nullptr && UserStatus != nullptr && ++g_state.progress_ticks == atla_after) {
        std::fprintf(stderr, "worlds: test: this copy (apart, player %d) collects atla %d on dungeon %d floor %d\n", Player(), atla_id, selectMapNo,
                     (int) UserStatus->cur_floor);
        std::fflush(stderr);
        slotsync::BagDump("before atla");
        getAtraToSaveData(atla_id, 0, SaveData, selectMapNo, UserStatus->cur_floor);
        slotsync::BagDump("atla");
    }

    // An apart copy's floor hooks (DC_NETPLAY_NEXT_FLOOR past the split tick), counted by its own floor play.
    if (g_state.apart) {
        NetplayTestFloorApart(g_state.split_tick + ++g_state.floor_ticks);
    }

    if (!apart_fired && g_state.apart && ApartExitDue()) {
        apart_fired = true;
        gameTask = GAME_TASK_EXIT_FADE_START;
        std::fprintf(stderr, "worlds: test: this copy (apart) takes the dungeon's exit\n");
        return;
    }

    // coop-floorstay test hook (DC_WORLDS_TEST_FLOOR_LEAVE=K, both copies; x-floor-stay): after K ticks linked on a floor (from a
    // floor merge's lockstep, counted alike on both copies), player 1 takes the dungeon's way out once, both copies on one tick, as his floor
    // menu's way out does (Jep's 2026-11-04 run): he leaves for the town, she stays on the floor.
    if (static const long long leave_at = TestEnvInt("DC_WORLDS_TEST_FLOOR_LEAVE", -1); leave_at >= 0) {
        static bool      left = false;
        static long long on_floor = 0;

        if (!left && g_state.up && !g_state.apart && mode == GAME_MODE_DUNGEON && !NetplayCatchingUp() && ++on_floor >= leave_at && BtEventMode == 0) {
            left = true;
            CoopWorldsExitBy(1);
            gameTask = GAME_TASK_EXIT_FADE_START;
            std::fprintf(stderr, "worlds: test: player 1 takes the floor's way out at link tick %lld\n", (long long) g_state.tick);
            return;
        }
    }

    if (exit_at < 0 || g_state.exit_fired || !g_state.up || !TestExitDue(exit_at)) {
        return;
    }

    // DC_WORLDS_EXIT_IN_MENU=N: not before player 1's own menu (beside the world) has been open N ticks, so the
    // split comes with his menu open, as in Jep's 2026-10-24 run (her stick then moved her quick-use slot).
    static const int in_menu = TestEnvInt("DC_WORLDS_EXIT_IN_MENU", -1);
    static int       menu_ticks = 0;

    if (in_menu >= 0) {
        // (coop-exitmenu: his menu as both copies record it, the actor table's bits: since local menus (localmenu 3b) his menu
        // runs on his copy alone and FullMenuOwner reads the lockstep menu only, so the hook never fired and he stood in it till
        // a monster downed him; q-p2-quickslot-p1menu, train 49)
        menu_ticks = coop::FullMenuOpenFor(1) ? menu_ticks + 1 : 0;
        if (menu_ticks < in_menu) {
            return;
        }
    }

    g_state.exit_fired = true;

    // DC_WORLDS_EXIT_BY=escape: by an Escape (the item's way out: GAME_TASK_ESCAPE_INIT, its effect and fade), as
    // Jep's Toan left on 2026-10-20, instead of the exit's fade alone.
    static const char *const by = TestEnv("DC_WORLDS_EXIT_BY");
    const bool               escape2 = by != nullptr && std::strcmp(by, "escape2") == 0; // coop-dungeonp2: player 2's menu Escape
    const bool               escape = escape2 || (by != nullptr && std::strcmp(by, "escape") == 0);

    if (escape2) {
        CoopWorldsExitBy(2);
    }

    gameTask = escape ? GAME_TASK_ESCAPE_INIT : GAME_TASK_EXIT_FADE_START;
    std::fprintf(stderr, "worlds: test: player 1 takes the dungeon's %s at link tick %lld\n", escape ? "Escape" : "exit", (long long) g_state.tick);
}

void CoopWorldsBagsBack() {
    slotsync::SwapBags(false);
}

void CoopWorldsBagsHers() {
    slotsync::SwapBags(true);
}

void CoopWorldsBagsSaveBack() {
    if (!Mutated("no_save_home")) {
        slotsync::SwapBags(false);
    }
}


void CoopWorldsBagsFollow() {
    FollowApart();
}

bool CoopWorldsPartnerOnFloor(int *map, int *floor) {
    if (!On() || !g_state.apart || g_state.peer.mode != GAME_MODE_DUNGEON || g_state.peer.floor == -1) {
        return false;
    }

    *map = g_state.peer.map;
    *floor = g_state.peer.floor;
    return true;
}
