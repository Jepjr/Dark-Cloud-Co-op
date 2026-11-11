#include "coop/game_ns.hpp"
#include "coop/coop.hpp"
#include "coop/coop_actor.hpp" // coop-playfix: whose screen this is (ActorLocalIndex); coop-localmenu: the in-menu bits
#include "coop/coop_worlds.hpp" // coop-worlds
#include "coop/coop_town.hpp"
#include "coop/coop_townmenu.hpp"
#include "coop/coop_inventory.hpp" // coop-bagapart: BagScreenTrace
#include "coop/coop_message.hpp"   // coop-closetglow: whose the system message is
#include "coop/coop_audio.hpp"     // coop-pt1106-batch: her talk's menu sounds are hers
#include "runscript.hpp" // coop-pt1106-batch: ScriptExtCalls (the event points test hook)
#include "sysmes.hpp" // coop-town: the help bar's trace
#include "coop/coop_georama.hpp" // coop-town: her own Georama
#include "coop/netplay.hpp"
#include "coop/netplay_test.hpp" // coop-localfix: NetplayMutated
#include "gfx/gfx.hpp"               // coop-mayorhouse: his talk window drawn out of sight

#include <libvu0.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <new>
#include <optional>
#include <string>
#include <utility>

#include "btitem.hpp"
#include "clsmes.hpp" // coop-mayorhouse: the talk window's place (DC_COOP_TEST_TALK_RECT)
#include "camerafollow.hpp"
#include "character.hpp"
#include "collision.hpp"
#include "dataalloc.hpp"
#include "dataread.hpp"
#include "dataset.hpp" // WorkBuffer (WorkBuffer__2 at 6ec3b808 was its alias)
#include "dun/gameloop.hpp"
#include "ebattle.hpp"
#include "gamepad.hpp" // coop-localfix: the door trace
#include "edit.hpp"
#include "boxvu0.hpp"
#include "editground.hpp"
#include "editloop.hpp"
#include "editloop3.hpp" // EdLimitShadowLight; coop-closet: EdGetEvent
#include "edit_in.hpp"   // coop-closet: EdInInfo, the room's event points
#include "editmapscript.hpp"
#include "frame.hpp"
#include "frameattr.hpp"
#include "main.hpp"
#include "mapparts.hpp"
#include "mglib.hpp"
#include "common.h" // coop-housefade: GS_Y_OFFSET
#include "render/gs_state.hpp"
#include "npcharacter.hpp" // coop-menuanim: the villagers' trace
#include "objanime.hpp"    // coop-menuanim: and the map's object animations
#include "texture.hpp"
#include "textureanime.hpp"
#include "savedata.hpp"
#include "dngstatusdata.hpp"
#include "userstatus.hpp"

#include "coop/coop_camera.hpp" // coop-drawrng: DrawRngEnter/Leave
#include "coop/coop_core.hpp"
#include "coop/coop_party.hpp"
#include "coop/coop_combat.hpp"
#include "coop/coop_wheel.hpp"
#include "coop/coop_spawn.hpp"
#include "coop/coop_interact.hpp" // coop-doorprompt: the picker at the cave door
#include "coop/coop_menu.hpp"     // coop-roomevent: MenuSwapP2Bag
#include "gamepad.hpp"       // coop-roomevent: PadUpdateFrom
#include "mathutil.hpp"           // coop-doorfix: AngleInterpolate
#include "menu_draw.hpp"          // coop-doorfix: set2DSpriteC4 (the user's fade)
#include "rect.hpp"
#include "snd.hpp"                // coop-doorfix: SndSePlay
#include "coop/coop_audio.hpp"     // coop-doorfix: SoundOwner
#include "coop/coop_present.hpp"   // coop-townchest: whose her box's message and sound are
#include "menu_save.hpp"          // coop-townchest: TransWepNo, GetAddAttachItem
#include "sound_effect.h"         // coop-townchest: SE_BOX_OPEN



namespace dc::game {
int keyCtrl(float x, float y, MOTION_INFO *motion);
extern int start_event_no; // editloop.cpp: the town's queued map event (DC_COOP_TEST_START_TRACE)
extern const std::array<const char *, 6> charaNameTbl; // btitem.cpp: each member's .chr pack
void EdGetFadeColor(float *colour); // editloop3: the editor fade (coop-roomevent)
extern s32 gameTask; // dun/gameloop.cpp (coop-localfix: CoopTownStep)
extern s32 mode;      // main.cpp (coop-doorfix: the cave door's acts are the town's)
extern int chara_mode; // ebattle.cpp (coop-doorfix: EdMoveChara keeps off an act's motion)
extern float camera_near_dist; // ebattle.cpp (coop-housecam)
extern float camera_far_dist;  // ebattle.cpp (coop-housecam)
extern int SystemMesNo;        // sysmes.cpp
struct EdCameraWallInfo {      // ebattle.cpp (coop-housecam: EdMoveChara's camera walls, run on her camera too)
    int left_clear;
    int right_clear;
    int keep_distance;
    int turned;
};
void EdCameraWalls(CCameraFollow *camera, CEditGround *ground, int shallow, int fishing, int turn_at_once, EdCameraWallInfo *info);
} // namespace dc::game
namespace dc::render {
unsigned long PortTextureMisses(); // render/texture_registry.hpp (coop-localfix: her model's draw misses)
} // namespace dc::render

namespace coop {

namespace {

// How far beside Toan the partner starts.
constexpr float kTownSpawn = 14.0f;
// How far it may fall below the last ground it stood on, and how long it may go without a floor
// under it, before it is rescued (it roams freely otherwise).
constexpr float kTownFall = 300.0f;
constexpr int   kTownAirborneTicks = 120;

struct Town {
    CCharacter    *body = nullptr;
    CTexAnimeData *anime = nullptr;
    int            loaded = -1;
    int            map = -1;
    bool           visible = false;
    int            motion = -1;
    float          propeller = 0.0f; // Osmond's propeller's turn (SpinPropeller)
    sceVu0FVECTOR  pos = {0.0f, 0.0f, 0.0f, 1.0f};
    sceVu0FVECTOR  velocity = {0.0f, 0.0f, 0.0f, 0.0f};
    int            shadows = 0; // shadows drawn since the last debug line
    int            shadow_alpha = 0;      // the last shadow's blend (MGEndDrawShadow)
    float          shadow_light_y = 0.0f; // the light's [1][0] it was drawn under
    float          scene_light_y = 0.0f;  // and the scene's, before EdLimitShadowLight
    int            airborne = 0;     // ticks without a floor under it
    bool           has_ground = false;
    sceVu0FVECTOR  ground = {0.0f, 0.0f, 0.0f, 1.0f}; // the last ground it stood on
    bool           high_camera = false; // standing on a high-camera floor (EdMoveChara's shallow)
    bool           stood = false;       // coop-nopush: her last step stood her by Toan, so her body walls his walk
};

Town g_town;
int  g_town_warp_ticks = 0; // TownWarpForTest's count, from the partner's last (re)load

// coop-inbuild: the partner inside a building. Whether this tick's step is indoors and whether her last step was (a
// change of side places her afresh, and her camera), the room's parts her walk collides with and its own camera, and
// her camera's marker among the room's fixed cameras, held as SetCameraPos holds Toan's (20 ticks before it changes).
struct Indoor {
    bool           on = false;
    bool           was = false;
    CMapParts     *parts = nullptr;
    int            parts_count = 0;
    CCameraFollow *room = nullptr;
    bool           markers = false;
    int            marker = -1;
    int            marker_hold = 0;
    int            ticks = 0; // her steps out of the room's events since she was placed in it (the tests' door warp)
    float          stick[2] = {}; // her stick at her last step (DC_COOP_DEBUG)
    int            mode = 0;      // the room's mode this tick (DC_COOP_DEBUG)
    long long      walked = -100; // coop-mayorhide: the link tick the room last stepped in walk mode
};

Indoor g_indoor;
static bool g_room_shown_before = false; // coop-housefade: she was shown in the room at the step before this one

// coop-roomevent: her room event (CoopTownHerRoomEvent, below).
struct RoomEvent {
    bool           on = false; // her event runs (from its start to the room's tick it ends on)
    int            event = -1;
    long long      tick = 0; // the link tick it started on
    CCharacter    *main = nullptr; // the event's main character and its texture animation before hers (Toan's)
    CTexAnimeData *anime = nullptr;
    int            anime_count = 0;
    CGamePad       pad{};
    int            player = 0;     // coop-small2: the player (1 up) whose pad and Cross started it; its script's menus and sounds are theirs
    bool           pad_in = false; // her pad and bag the game's (StepIn to StepOut)
    bool           bag_in = false;
    const void    *program = nullptr; // the room's script, as the room's walk last gave it (her mark's test)
    int            frames_hidden = 0; // his copy: frames its pictures were drawn out of sight
    long long      pressed = -1;      // the link tick her Cross last started a room event on (the test's frames)
    const char    *view = "none";     // the camera this copy last drew the room from while it ran (the test's lines)
    unsigned       motions_said = 0;  // coop-xiaopose: Toan's motions (by number, under 32) her body was asked for, said once
    int            villager = -1;     // coop-hertalk: the villager her Cross talks to (its talk set up), -1 for a room event
    int            talk_frames_hidden = 0; // his copy: frames her talk's window was drawn out of sight
    float          toan_from[3] = {};      // coop-hertalk: where Toan stood as her talk began, and the farthest he walked from it
    float          toan_far = 0.0f;
    long long      talk_tick = -1;         // coop-hertalk: the link tick her talk began on
    bool           his_talk_placed = false; // coop-talks2 test hook: Toan put before the villager (DC_COOP_TEST_HIS_TALK)
    long long      ended = -1;              // coop-joinfix: the link tick her last event or talk ended on
    bool           held_up = false;         // coop-closetitem: the script's last motion for her was Toan's item held up (6)
    float          cam_angle_from = 0.0f;   // opt8: her camera's angle as her talk began
} g_room_event;

// opt8 (Jep 11-09: one talk path for every player): each player's talk in a slot of its own, indexed as the actor table
// (entry 0 the game's player, the resident; entry 1 player 2), holding what retail's one talk keeps in statics: editloop3's
// talk state (EdTalkStateSwap's block) and the message window. Together every talk runs from its slot through one step
// (CoopTalkStep) and one draw (CoopTalkDrawBegin/End), on every copy from the agreed pads; retail's talk mode stays the
// game's player's alone apart. Carried in the snapshot extras (CoopTownTalksBytes), so a merge or resync carries every talk.
struct TalkSlot {
    alignas(16) unsigned char state[256];
    alignas(16) unsigned char window[sizeof(ClsMes)];
    bool      on;       // the talk runs
    long long tick;     // the link tick it began on
    int       event;    // its result's event (retail's result 4), to start as its talker's own on the next free step; 0 none
};
TalkSlot g_talks[coop::kMaxPlayers] = {};

int  GatherTownPolys(const float *pos, CCPoly **out);
void TownSecondSpot(const float *spot, float yaw, const float *other, float *out);

// While player 2's copy draws the town from player 2's offset.
struct TownView {
    bool         active = false;
    bool         rand_held = false; // coop-localfix: the draw takes the draw stream until CoopTownViewEnd (coop-drawrng)
    bool         own = false; // the frame is drawn from player 2's own camera (OwnTownView)
    float        eye[3] = {}; // and its eye, and that eye's height over her feet (CoopTownDrawnEye)
    float        over = 0.0f;
};

TownView g_town_view;
float    g_drawn_eye[3] = {}; // coop-pt1109e: the eye and view axis this copy last drew from
float    g_drawn_ref[3] = {};

// The town's draw takes the draw stream once a frame, however many of the view's branches hold it (coop-drawrng).
void HoldDrawRng() {
    if (!g_town_view.rand_held) {
        g_town_view.rand_held = true;
        DrawRngEnter();
    }
}

InputPadState Pad2() {
    InputPadState linked;

    return NetplayPad(1, linked) ? linked : InputGetPad(1);
}

// The party in town: the save's dungeon status (UserStatus is the dungeon's and unset here).
int PartySize() {
    return SaveData->GetDngStatus()->party_size;
}

// Whether pad 2 may play chara in town: never Toan (the town's player), and a member who has joined;
// in a party of one, any member stands in, as in the dungeons.
bool TownPickable(int chara) {
    return chara > 0 && chara < 6 && (chara < PartySize() || PartySize() <= 1);
}


// The member pad 2 plays in town: its pick from the dungeon, if that member has joined, else Xiao
// (Toan is the town's player; a party of one gets Xiao standing in).
int TownMember() {
    // Test hook: DC_COOP_TOWN_MEMBER=n has pad 2 play member n in town (a --jump save has Toan alone).
    static const int forced = TestEnvInt("DC_COOP_TOWN_MEMBER", -1);

    if (forced > 0 && forced < 6) {
        return forced;
    }

    return TownPickable(g_partner.choice) ? g_partner.choice : CHARA_XIAO;
}

void PlaceBesideToan(const char *reason) {
    sceVu0FVECTOR at;
    sceVu0FVECTOR rotation;

    Chara->GetPosition(at);
    Chara->GetRotation(rotation);
    g_town.pos[0] = at[0] - kTownSpawn * cosf(rotation[1]);
    g_town.pos[1] = at[1];
    g_town.pos[2] = at[2] + kTownSpawn * sinf(rotation[1]);
    g_town.pos[3] = 1.0f;
    g_town.velocity[0] = g_town.velocity[1] = g_town.velocity[2] = 0.0f;
    g_town.body->frame->SetPosition(g_town.pos);
    g_town.body->frame->SetRotation(0.0f, rotation[1], 0.0f);
    g_town.body->SetPosition(g_town.pos);
    g_town.body->SetRotation(0.0f, rotation[1], 0.0f);
    std::printf("coop: P2 placed beside Toan (%s) at (%.0f %.0f %.0f)\n", reason, g_town.pos[0], g_town.pos[1], g_town.pos[2]);
}

// coop-spawn: a town merge keeps player 2 where her copy had her (the host's stand, the same on both copies) instead of
// beside Toan; if the two stand on one spot, the one who arrived last moves to spot 2 of its arrival. Without a stand (no
// merge, or DC_COOP_MUTATE=spawn_reset_stayer on the host) she goes beside Toan, as before. DC_COOP_MUTATE=spawn_one_spot
// (twin): nobody moves, both on one spot.
SpawnStand g_merge_place; // coop-localfix: where TownMergedOutdoors put her at a snapshot merge, for the host's town load to keep
bool       g_stand_after_event = false; // coop-soakfix3: her stand waits for the town event's end (kSpawnAfterEvent; every copy alike)
bool       TownGroundAt(float x, float y, float z, float *ground, int *kind); // (below)

void PlaceAfterLoad(const char *reason) {
    SpawnStand stand;

    // (coop-mergestand: Toan's own stand after a town resync is applied by the town's walk step, CoopWorldsTestGateWarp,
    // on a tick both copies share; this load's tick can differ between them, and his position is in the checksum.)
    if (!SpawnTakeStand(MapNo, &stand)) {
        PlaceBesideToan(reason);
        return;
    }

    sceVu0FVECTOR toan;
    sceVu0FVECTOR rotation;

    Chara->GetPosition(toan);
    Chara->GetRotation(rotation);
    g_town.pos[0] = stand.pos[0];
    g_town.pos[1] = stand.pos[1];
    g_town.pos[2] = stand.pos[2];
    g_town.pos[3] = 1.0f;

    float yaw = stand.yaw;
    const char *moved = "nobody moves";

    bool merge_placed = false;

    // coop-localfix: a snapshot merge placed her on both copies on its tick (TownMergedOutdoors): kept.
    if (g_merge_place.set && g_merge_place.map == MapNo) {
        g_town.pos[0] = g_merge_place.pos[0];
        g_town.pos[1] = g_merge_place.pos[1];
        g_town.pos[2] = g_merge_place.pos[2];
        yaw = g_merge_place.yaw;
        moved = "placed at the merge (both copies)";
        merge_placed = true;
    }
    g_merge_place.set = false;

    // coop-localfix (Jep's 2026-10-26 run: a reload as he came out of the mayor's door onto her): this runs in the town's load,
    // which a merge by catch-up (a snapshot) has on the host's copy alone (hers lands where it stands, with no load): moving
    // either player here put him (in the checksum) or her somewhere else on his copy only. Such a merge moves nobody: each
    // stands where their own copy had them. DC_COOP_MUTATE=spawn_moves_toan (twin): the spot-2 move as before.
    const bool one_copy_load = NetplayLinkedBySnapshot() && !Mutated("spawn_moves_toan");

    if (SpawnOccupied(toan, g_town.pos) && !Mutated("spawn_one_spot") && !one_copy_load && !merge_placed) {
        float spot[3];

        if (stand.arriver == 2 || stand.arriver == kSpawnAfterEvent) {
            TownSecondSpot(g_town.pos, stand.yaw, toan, spot);
            g_town.pos[0] = spot[0];
            g_town.pos[1] = spot[1];
            g_town.pos[2] = spot[2];
            moved = "player 2 arrived last: she takes spot 2";
        } else {
            TownSecondSpot(toan, rotation[1], g_town.pos, spot);
            toan[0] = spot[0];
            toan[1] = spot[1];
            toan[2] = spot[2];
            Chara->SetPosition(toan);
            Chara->frame->SetPosition(toan);
            moved = "player 1 arrived last: he takes spot 2";
        }
    }

    g_town.velocity[0] = g_town.velocity[1] = g_town.velocity[2] = 0.0f;
    g_town.body->frame->SetPosition(g_town.pos);
    g_town.body->frame->SetRotation(0.0f, yaw, 0.0f);
    g_town.body->SetPosition(g_town.pos);
    g_town.body->SetRotation(0.0f, yaw, 0.0f);
    if (stand.arriver == kSpawnAfterEvent) {
        g_stand_after_event = true;
        moved = "her stand waits for the town event's end";
    }
    const float dx = g_town.pos[0] - toan[0];
    const float dz = g_town.pos[2] - toan[2];

    std::printf("coop: P2 placed where she stood (%s) at (%.0f %.0f %.0f), stand (%.0f %.0f %.0f), Toan at (%.0f %.0f %.0f), %.0f apart; %s\n", reason,
                g_town.pos[0], g_town.pos[1], g_town.pos[2], stand.pos[0], stand.pos[1], stand.pos[2], toan[0], toan[1], toan[2],
                std::sqrt(dx * dx + dz * dz), moved);
    std::fflush(stdout);
    // coop-localfix (Jep's 2026-10-26 note 2: "when Toan leaves, Xiao gets warped to next to him"): she is where she stood,
    // on this side of every door now. His copy, back out of the mayor's house, still had her step's last place indoors
    // (his room's), so her next step took the room's door out and put her "beside Toan (out of a building)" on his copy
    // alone. DC_COOP_MUTATE=stand_then_beside (twin): left as it was.
    if (!Mutated("stand_then_beside")) {
        g_indoor.was = EdInteriorFlag != 0;
    }
}

bool BlockEmptyNow() {
    for (const CTexture &texture : TexManager.textures) {
        if (texture.block == PartnerTextureBlock()) {
            return false;
        }
    }

    return true;
}

// The member's dungeon pack (only Toan has a town one) into the partner's arena and texture block,
// which the dungeon reloads for itself when it next starts a floor.
bool g_reload_in_place = false; // coop-localfix: CoopTownSnapshotApplied's load keeps her place (its blocks were emptied on purpose)
bool  g_join_place = false;     // coop-joindesync: her copy's first load after a join keeps the state set's place and facing
float g_join_yaw = 0.0f;
bool  g_file_load_due = false; // coop-townstand: a state file's partner to load right after its apply (CoopTownSnapshotApplied)

bool TownLoad(int chara) {
    // Another member in the same town (pad 2's wheel): it takes the place and facing of the one before.
    const bool    join = g_join_place && g_town.loaded < 0 && !g_reload_in_place;
    bool          swap = g_reload_in_place || join ||
                g_town.loaded >= 0 && g_town.map == MapNo && g_town.body != nullptr && g_town.body->frame != nullptr && !BlockEmptyNow();
    const char   *why = g_town.loaded < 0 ? "town load: first" : g_town.map != MapNo ? "town load: new map" : "town load: town reloaded";
    sceVu0FVECTOR stood = {g_town.pos[0], g_town.pos[1], g_town.pos[2], 1.0f};
    sceVu0FVECTOR facing = {0.0f, 0.0f, 0.0f, 0.0f};

    g_join_place = false;
    if (join) {
        facing[1] = g_join_yaw;
        std::printf("coop: her first town load after the join keeps the host's place (%.0f %.0f %.0f)\n", stood[0], stood[1], stood[2]);
    } else if (swap) {
        g_town.body->frame->GetRotation(facing);
    }

    if (g_partner.pack == nullptr) {
        g_partner.pack = reinterpret_cast<u_int *>(new (std::align_val_t{64}) u_char[kPackBytes]);
        g_partner.model = new (std::align_val_t{64}) u_char[static_cast<size_t>(kModelQuads) * 16];
    }

    if (g_town.body == nullptr) {
        g_town.body = new CCharacter();
        g_town.anime = new CTexAnimeData[64];
    }

    int size = 0;

    if (LoadFile(const_cast<char *>(charaNameTbl[chara]), g_partner.pack, &size) == 0 || size <= 0 || size > kPackBytes) {
        std::fprintf(stderr, "coop: cannot load %s for town\n", charaNameTbl[chara]);
        return false;
    }

    TexManager.DeleteTextureBlock(PartnerTextureBlock());
    TexManager.DeleteTextureBlock(PartnerWeaponBlock());
    TexManager.CleanUpBuffer();
    TexManager.CleanUpTextureList();

    CDataAlloc2<1> &arena = g_partner.arena;

    arena.base = g_partner.model;
    arena.buffer = g_partner.model;
    arena.used = 0;
    arena.limit = kModelQuads;

    // The dungeon partner's model shared these buffers; it loads again on the next floor.
    g_partner.loaded_chara = -1;

    CCharacter *body = g_town.body;

    body->Initialize();

    for (int i = 0; i < 64; i++) {
        g_town.anime[i].Initialize();
    }

    body->InitializeTexAnime(g_town.anime, 64);
    body->LoadPackData2(g_partner.pack, (char *) "base.cfg", &arena, PartnerTextureBlock(), &arena, 0);

    if (body->frame == nullptr) {
        return false;
    }

    CFrameAttr attr;

    attr.fog_enable = true;
    attr.clip_enable = false;
    attr.program_option = 0;
    body->frame->SetAttr(attr, 1, 4);
    // coop-doorfade (Jep, 2026-10-27: "both players ... no longer ... hear P2's footsteps"; older: no Xiao footsteps): her steps
    // sound on both copies, as Toan's do, by the ground under her (PartnerStep). DC_COOP_MUTATE=town_silent (twin): off, as before.
    body->FootSoundEnable(Mutated("town_silent") ? 0 : 1);
    body->EventEnable(0);
    // The weapon in its hand, as in a dungeon: the members' town walks are their dungeon walks, posed
    // to hold it, and only Toan has a town model of his own (Jep, 2026-10-20). It goes just above the body.
    g_partner.weapon_mark = arena.used;
    g_partner.weapon_loaded = false;

    if (!Mutated("town_weapon")) {
        LoadWeapon(chara, body);
    }

    g_town.loaded = chara;
    g_town.map = MapNo;
    g_town.motion = -1;
    g_town_warp_ticks = 0;
    if (swap) {
        sceVu0CopyVector(g_town.pos, stood);
        body->frame->SetPosition(g_town.pos);
        body->frame->SetRotation(0.0f, facing[1], 0.0f);
        body->SetPosition(g_town.pos);
        body->SetRotation(0.0f, facing[1], 0.0f);
    } else {
        g_town.airborne = 0;
        g_town.has_ground = false;
        PlaceAfterLoad(why); // coop-spawn
    }

    std::printf("coop: town partner %d (%s) in town %d\n", chara, charaNameTbl[chara], MapNo);
    return true;
}

// The town's collision round pos, as EdMoveChara gathers Toan's: the ground's polygons outdoors, the
// interior's parts indoors.
int GatherTownPolys(const float *pos, CCPoly **out) {
    WorkBuffer->used = 0;
    CCPoly *polys = (CCPoly *) WorkBuffer->Alloc(0x7D0);
    int     count = 0;

    if (!g_indoor.on && EdMoveCharaInfo.interior == 0) {
        CEditGround *ground = EdMoveCharaInfo.ground != nullptr ? EdMoveCharaInfo.ground : pEditGround;

        count = ground->PickUpPoly(polys, pos[0], pos[1], pos[2]);
    } else {
        CBoxVu0 box;

        box.max[0] = pos[0] + 20.0f;
        box.min[0] = pos[0] - 20.0f;
        box.max[2] = pos[2] + 20.0f;
        box.min[2] = pos[2] - 20.0f;
        box.max[1] = pos[1] + 1000.0f;
        box.min[1] = pos[1] - 1000.0f;

        // coop-inbuild: the room's parts as EditInLoop gave them (EdMoveCharaInfo's are Toan's last walk's).
        CMapParts *parts = g_indoor.on ? g_indoor.parts : EdMoveCharaInfo.parts;
        const int  parts_count = g_indoor.on ? g_indoor.parts_count : EdMoveCharaInfo.parts_count;

        for (int i = 0; parts != nullptr && i < parts_count; i++) {
            CFrame *frame = parts[i].GetCollisionFrame();

            if (frame != nullptr) {
                count += frame->PickUpNearPoly(&polys[count], box);
            }
        }
    }

    *out = polys;
    return count;
}

// coop-p2collide (Jep's 2026-10-27 playtest: "P2 has no collision with players or NPCs" while both are in the mayor's house;
// apart, her copy's walk is Toan's and she bumps the mayor): the bodies EdMoveChara adds to Toan's walls, for hers: the event
// points' (EdEventPointCpPoly), each villager's in play, and Toan's own (she stops at him, never pushes him). Appended at
// out. DC_COOP_MUTATE=walk_through (twin): the room's and the town's polygons only, as before.
// coop-nopush (Jep's 2026-10-27 playtest: "P1 can push P2 around"): Toan's only while she moves. Standing still, his wall's
// MoveCheck stepped her off him as he walked up to her; now he stops at her (CoopTownPartnerWall). DC_COOP_MUTATE=
// push_through (twin): his wall always, and none for him.
int PartnerBodyPolys(float *pos, bool moving, CCPoly *out) {
    if (Mutated("walk_through")) {
        return 0;
    }

    ED_EVENT_POINT *points = g_indoor.on ? (EdInInfo != nullptr ? EdInInfo->event_points : nullptr) : EdMoveCharaInfo.points;
    const int       point_count = g_indoor.on ? 32 : EdMoveCharaInfo.point_count;
    int             count = points != nullptr ? EdEventPointCpPoly(pos, points, point_count, out, NowTime) : 0;

    for (CNPCharacter &villager : EdVillager) {
        if (villager.initialized != 0 && villager.draw_enabled != 0) { // EdMoveChara's IsVillagerActive
            count += villager.PickUpPoly(pos, &out[count]);
        }
    }

    if (Chara != nullptr && Chara->frame != nullptr && (moving || Mutated("push_through"))) {
        count += Chara->PickUpPoly(pos, &out[count]);
    }

    return count;
}

// coop-doorfade: DC_COOP_DEBUG: footstep sounds (CCharacter::Step sets foot_sound_wait to 5 on one and counts it down once in
// the same step), each 60 steps, by whose: "her" (the partner's walk); coop-footsteps: "skin" (her skin over Chara apart) and
// "own" (Chara under it) (stderr; the footstep tests read them).
static void FootstepNote(const CCharacter *body, const char *whose) {
    static const bool enabled = CoopDebug();
    static int        steps[3] = {};
    static int        sounds[3] = {};

    if (!enabled || body == nullptr) {
        return;
    }

    const int i = whose[0] == 'h' ? 0 : whose[0] == 's' ? 1 : 2;

    sounds[i] += body->foot_sound_wait == 4;
    if (++steps[i] % 60 == 0) {
        std::fprintf(stderr, "coop-town: p%d %s footsteps %d in 60 steps (sound id %d, enabled %d)\n", NetplayLocalPlayer(), whose, sounds[i],
                     body->foot_sound_id, body->foot_sound_enable);
        sounds[i] = 0;
    }
}

// coop-p2ground: the ground her walk stands on at (x, z), from at most kGroundReach above or below y: the town's or the
// room's polygons, and indoors the room's floor at 0 where its parts have none (EdMoveChara's floor, as her walk takes it).
// coop-townstand: kind, when asked, the polygon's ground event (attr.ground_kind, the event her walk onto it starts; 0 none,
// and 0 on the room's floor).
bool TownGroundAt(float x, float y, float z, float *ground, int *kind) {
    constexpr float kGroundReach = 8.0f;
    sceVu0FVECTOR   at = {x, y, z, 1.0f};
    CCPoly         *polys;
    const int       count = GatherTownPolys(at, &polys);
    CCPoly          poly;
    sceVu0FVECTOR   hit;
    bool            found = false;

    at[1] = y + kGroundReach;
    if (count > 0 && GetFootPoly(at, 2.0f * kGroundReach, &poly, hit, polys, count, 0)) {
        *ground = hit[1];
        found = true;
        if (kind != nullptr) {
            *kind = poly.attr.ground_kind;
        }
    }

    if (kind != nullptr && !found) {
        *kind = 0;
    }

    if ((g_indoor.on || EdMoveCharaInfo.interior != 0) && (!found || *ground < 0.0f) && std::fabs(y) <= kGroundReach) {
        *ground = 0.0f;
        found = true;
    }

    return found;
}

// coop-toanvis2: whether a walk from from to to gets there: her walk's MoveCheck against the town's or the room's polygons and
// the bodies in them (PartnerBodyPolys: event points, villagers; not Toan), in four-unit steps, each stopped short by less
// than a fifth. Level: the ground under to is TownGroundAt's.
static bool TownStepClear(const float *from, const float *to) {
    const float dx = to[0] - from[0];
    const float dz = to[2] - from[2];
    const int   steps = std::max(1, static_cast<int>(std::ceil(std::sqrt(dx * dx + dz * dz) / 4.0f)));
    sceVu0FVECTOR pos = {from[0], from[1], from[2], 1.0f};
    sceVu0FVECTOR step = {dx / steps, 0.0f, dz / steps, 0.0f};

    for (int i = 0; i < steps; i++) {
        CCPoly       *polys;
        int           count = GatherTownPolys(pos, &polys);
        sceVu0FVECTOR moved;
        MoveCheckInfo info;

        count += PartnerBodyPolys(pos, false, &polys[count]);
        MoveCheck(pos, step, moved, &info, polys, count, 0);
        if ((moved[0] - pos[0]) * step[0] + (moved[2] - pos[2]) * step[2] < 0.8f * (step[0] * step[0] + step[2] * step[2])) {
            return false;
        }
        pos[0] = moved[0];
        pos[2] = moved[2];
    }

    return true;
}

// coop-p2ground (Jep's 2026-10-27 run: "P2 spawning to the side of P1 can cause P2 to fall off the stairs and clip into the
// house"): spot 2 as SpawnSecondSpot picks it, on her ground. The farther side first, then the nearer, then a spot behind
// and ahead of the facing (coop-toanvis2: then ahead to each side, then farther ahead), each taken only where her walk has ground within a step of the spot's height (and put on it);
// none: the spot itself, on Toan sooner than off the edge. The same on both copies (their ground is the same).
// DC_COOP_MUTATE=aside_off_ground (twin): SpawnSecondSpot's side, wherever it lands.
// coop-toanvis2 (Jep's 2026-10-31 sweep: "Toan invisible" in the mayor's house on P2's screen, only his boots and weapon): a
// room's floor stands at 0 under every spot (TownGroundAt), walls or not, so his step aside at her merge (he came in on her
// spot) took the side along the door's wall, behind the closed door, which hid all of him but his feet. A spot is taken only
// where a walk from the spot gets to it (TownStepClear). DC_COOP_MUTATE=aside_through_wall (twin): ground alone, as before.
// coop-townstand (fx51b's town debug start: a pair joining in Queens, P2's join stand spot 2 of Toan's (1507 170 32) lay on
// the town's dungeon-entry polygon, her walk's first landing started its event 1, and her copy left for the forest at tick
// 38 with no input; Matataki's the same into Wise Owl): a spot is taken only on ground with no event of its own (ground
// kind 0), as nobody stood there to choose it. DC_COOP_MUTATE=spot_in_event (twin): any ground, as before.
void TownSecondSpot(const float *spot, float yaw, const float *other, float *out) {
    constexpr float kStep = 6.0f;

    SpawnSecondSpot(spot, yaw, other, out);
    if (Mutated("aside_off_ground")) {
        return;
    }

    const float first[3] = {out[0], out[1], out[2]};
    const float ax = kSpawnSpot * std::sin(yaw);
    const float az = kSpawnSpot * std::cos(yaw);
    // coop-toanvis2: then ahead and to each side (45 degrees), then twice as far ahead: at the mayor's door both sides are wall,
    // behind is the door and straight ahead is her.
    const float rx = first[0] - spot[0];
    const float rz = first[2] - spot[2];
    constexpr float kDiagonal = 0.70710678f;
    const float     tries[9][2] = {{first[0], first[2]},
                                   {2.0f * spot[0] - first[0], 2.0f * spot[2] - first[2]},
                                   {spot[0] - ax, spot[2] - az},
                                   {spot[0] + ax, spot[2] + az},
                                   {spot[0] + kDiagonal * (ax + rx), spot[2] + kDiagonal * (az + rz)},
                                   {spot[0] + kDiagonal * (ax - rx), spot[2] + kDiagonal * (az - rz)},
                                   {spot[0] + 2.0f * ax, spot[2] + 2.0f * az},
                                   {spot[0] + 2.0f * kDiagonal * (ax + rx), spot[2] + 2.0f * kDiagonal * (az + rz)},
                                   {spot[0] + 2.0f * kDiagonal * (ax - rx), spot[2] + 2.0f * kDiagonal * (az - rz)}};

    for (const auto &t : tries) {
        const float at[3] = {t[0], spot[1], t[1]};
        float       ground = 0.0f;
        int         kind = 0;

        if (SpawnOccupied(at, other) || !TownGroundAt(t[0], spot[1], t[1], &ground, &kind) || std::fabs(ground - spot[1]) > kStep) {
            continue;
        }
        if (kind > 0 && !Mutated("spot_in_event")) {
            std::printf("coop: spot 2 at (%.0f %.0f %.0f) is on town event %d's ground\n", at[0], ground, at[2], kind);
            continue;
        }
        if (!Mutated("aside_through_wall") && !TownStepClear(spot, at)) {
            std::printf("coop: spot 2 at (%.0f %.0f %.0f) is past a wall from (%.0f %.0f %.0f)\n", at[0], at[1], at[2], spot[0], spot[1], spot[2]);
            continue;
        }

        out[0] = t[0];
        out[1] = ground;
        out[2] = t[1];
        if (&t != &tries[0]) {
            std::printf("coop: spot 2 at (%.0f %.0f %.0f) has no ground or is past a wall: steps aside to (%.0f %.0f %.0f)\n", first[0], first[1], first[2], out[0], out[1],
                        out[2]);
        }
        return;
    }

    // coop-soakfix3: rings outward (8 ways, 2 to 12 spots out), the first spot on her ground's level, off any town event's ground and
    // reached by a walk from the spot: a dungeon gate's ground wider than the tries above (Matataki's, round the forest's way in) left
    // her on it, and her first landing there took her copy into the dungeon alone. DC_COOP_MUTATE=spot_ring_narrow (twin): the tries
    // above only, as before.
    if (!Mutated("spot_ring_narrow")) {
        for (int ring = 2; ring <= 12; ring++) {
            for (int way = 0; way < 8; way++) {
                const float angle = yaw + way * 0.785398f;
                const float t[2] = {spot[0] + ring * kSpawnSpot * std::sin(angle), spot[2] + ring * kSpawnSpot * std::cos(angle)};
                const float at[3] = {t[0], spot[1], t[1]};
                float       ground = 0.0f;
                int         kind = 0;

                if (SpawnOccupied(at, other) || !TownGroundAt(t[0], spot[1], t[1], &ground, &kind) || std::fabs(ground - spot[1]) > kStep || kind > 0 ||
                    !TownStepClear(spot, at)) {
                    continue;
                }
                out[0] = t[0];
                out[1] = ground;
                out[2] = t[1];
                std::printf("coop: spot 2 round (%.0f %.0f %.0f) is all on event ground or walls: ring %d out, at (%.0f %.0f %.0f)\n", spot[0], spot[1], spot[2],
                            ring, out[0], out[1], out[2]);
                return;
            }
        }
    }

    std::printf("coop: spot 2 has no ground round (%.0f %.0f %.0f): she stays where she stood\n", spot[0], spot[1], spot[2]);
    out[0] = spot[0];
    out[1] = spot[1];
    out[2] = spot[2];
}

void TownDebugLog() {
    static const bool enabled = CoopDebug();
    static int        wait = 0;

    if (!enabled || --wait > 0) {
        return;
    }

    wait = 60;

    sceVu0FVECTOR toan;

    Chara->GetPosition(toan);
    std::printf("coop-debug: town %d p1 (%.0f %.0f %.0f) p2 (%.0f %.0f %.0f) member %d motion %d time %.1f shadows %d weapon %d propeller %.2f clock %.3f save clock %.3f\n",
                MapNo, toan[0], toan[1], toan[2], g_town.pos[0], g_town.pos[1], g_town.pos[2], g_town.loaded, g_town.motion,
                g_town.body->motion_type.state.time, g_town.shadows, (int) g_partner.weapon_loaded, PropellerTurn(g_town.body), NowTime,
                SaveData != nullptr ? SaveData->GetNowTime() : -1.0f); // (coop-fringefix: the save's own clock beside NowTime)
    g_town.shadows = 0;
    std::printf("coop-debug: town shadow light %.3f scene %.3f alpha %d\n", g_town.shadow_light_y, g_town.scene_light_y,
                g_town.shadow_alpha);
}

// Player 2's own town camera (linked play): simulation state, stepped on both copies from pad 2's
// shared pad (right stick and R1/L1 turn it, as the town's MoveCamera turns Toan's from pad 1),
// following the partner as EdMoveChara's walk camera follows Toan, and pulled in front of the town's
// camera walls. Player 2's copy draws from it and pad 2 walks relative to it; Toan's camera is never
// touched. Couch play keeps one screen and Toan's camera.
struct TownCam {
    CCameraFollow *camera = nullptr;
    bool           ready = false;
    int            map = -1;
    float          distance = 60.0f;
    sceVu0FVECTOR  last = {0.0f, 0.0f, 0.0f, 0.0f}; // coop-xiaocam: her place on the last step, for her stride
    bool           indoor = false; // coop-herexit2: made in a room (IndoorCamInit, or her merge's room camera)
    float          swing = 0.0f;   // coop-housecam: what is left of her turn round from a wall (radians)
};

TownCam g_town_cam;

bool Linked() {
    return NetplayHasPlayer();
}

void TownCamFollow() {
    g_town_cam.camera->SetFollow(g_town.pos[0], g_town.pos[1] + 14.0f, g_town.pos[2]);
}

void TownCamInit() {
    if (g_town_cam.camera == nullptr) {
        g_town_cam.camera = new CCameraFollow(60.0f, 20.0f, 0.0f, 4.0f);
    }

    CCameraFollow *camera = g_town_cam.camera;

    camera->SetSpeed(7.0f);
    camera->FollowOn();
    camera->SetAngleSoon(MainCamera.GetAngle());
    // coop-localplay test hook (DC_COOP_TEST_HER_CAM_SKEW=S:D, the copy of local seat S, its first one only): its own camera
    // made D degrees off Toan's, as one her copy made from his camera at another tick than his copy did
    // (l-local-play-herwalk~twin: with her copy joining from its title that came only from load timing, 1 run in 3). Her
    // camera from the join's state set replaces it; DC_COOP_MUTATE=town_cam_own keeps it, and her stick then walks her
    // another way on her copy. (The seat in the value, not a player number in the code: the n-player lint.)
    static int skew_seat = -1;
    static int skew = 0;

    if (skew_seat < 0) {
        const char *value = TestEnv("DC_COOP_TEST_HER_CAM_SKEW");

        skew_seat = 0;
        if (value != nullptr && std::sscanf(value, "%d:%d", &skew_seat, &skew) != 2) {
            skew_seat = 0;
        }
    }
    if (skew != 0 && skew_seat == NetplayLocalPlayer()) {
        camera->SetAngleSoon(MainCamera.GetAngle() + skew * 3.14159265f / 180.0f);
        std::printf("coop: test: her own town camera made %d degrees off Toan's (seat %d)\n", skew, skew_seat);
        skew = 0;
    }
    camera->SetDistance(MainCamera.GetDistance());
    camera->SetHeight(MainCamera.GetHeight());
    g_town_cam.distance = MainCamera.GetDistance();
    TownCamFollow();
    camera->Step(-1);
    sceVu0CopyVector(g_town_cam.last, g_town.pos);
    g_town_cam.ready = true;
    g_town_cam.map = MapNo;
    g_town_cam.indoor = false;
    g_town_cam.swing = 0.0f;
}

void TownCamStep(const InputPadState &pad) {
    if (!g_town_cam.ready || g_town_cam.map != MapNo) {
        TownCamInit();
    }

    CCameraFollow *camera = g_town_cam.camera;
    sceVu0FVECTOR  look = {g_town.pos[0], g_town.pos[1] + 14.0f, g_town.pos[2], 1.0f};

    // coop-pt1109b (Jep's 2026-11-09 playtest: "both outside: P2's camera doesn't move in when she talks to a villager, his does"): her
    // talk's camera (EdTalkMode, given hers) has her follow off and sits at the talk's offsets; it only steps (retail's TalkCamera Step(1)).
    if (CoopTownHerTalkCamera() != nullptr) { // (stepped by her talk's own step: CoopTownHerTalkStepOut)
        return;
    }

    // coop-housecam test hook (DC_COOP_TEST_HER_CAM_SPIN=T:D, set on both copies): from link tick T her camera turns D degrees
    // each step, as R1 held would, round every wall by where she stands (t-p2-house-cam).
    static int   spin_from = -2;
    static float spin = 0.0f;

    if (spin_from == -2) {
        const char *value = TestEnv("DC_COOP_TEST_HER_CAM_SPIN");

        spin_from = -1;
        if (value != nullptr && std::sscanf(value, "%d:%f", &spin_from, &spin) != 2) {
            spin_from = -1;
        }
    }
    if (spin_from >= 0 && NetplayLinkTick() >= spin_from) {
        camera->AddAngle(spin * DEG_TO_RAD);
    }

    // Her camera walls are EdMoveChara's, on her camera (coop-housecam; Jep, 2026-11-04: "I would prefer P2+ to operate as
    // closely as possible to P1 does in the decompile"): EdCameraWalls slides the eye along a wall behind her, keeps it from
    // turning into one beside it, holds it over the floor, and under half the near distance turns it round. Toan's turns at
    // once; hers swings round kSwingStep a step (train 51: at once it moved her eye 105 in a frame as she walked by a wall
    // before his house-exit merge; the merge rows hold her eye under 15 and her view under 3 degrees a frame). The swing is
    // simulation state (TownSync::cam_swing). Before this her camera had its own wall ray only (coop-campulse, coop-housecam's
    // first turn): the eye sat at the back of her head by a house, then turned at once, then spun where both sides were close.
    // DC_COOP_MUTATE=towncam_turn_snap (twin): her turn at once, as Toan's.
    // DC_COOP_MUTATE=towncam_wall_from_eye (t-p2-wall-cam~twin): her old wall ray to the stepped eye (coop-campulse's twin).
    constexpr float  kSwingStep = 2.0f * DEG_TO_RAD;
    EdCameraWallInfo walls = {1, 1, 1, 0};

    if (!g_indoor.on && EdMoveCharaInfo.interior == 0) {
        CEditGround *ground = EdMoveCharaInfo.ground != nullptr ? EdMoveCharaInfo.ground : pEditGround;

        if (Mutated("towncam_wall_from_eye")) {
            sceVu0FVECTOR eye;
            sceVu0FVECTOR hit;
            CBoxVu0       box;
            const float   span = g_town_cam.distance + 20.0f;

            camera->GetPos(eye);
            box.max[0] = look[0] + span;
            box.min[0] = look[0] - span;
            box.max[2] = look[2] + span;
            box.min[2] = look[2] - span;
            box.max[1] = look[1] + 100.0f;
            box.min[1] = look[1] - 100.0f;
            WorkBuffer->used = 0;

            CCPoly *polys = (CCPoly *) WorkBuffer->Alloc(0x7D0);
            int     count = ground->PickUpCameraPoly(polys, box, 0xFFFF);

            camera->SetDistance(count > 0 && CheckHit(polys, count, look, eye, hit, 1, 0) >= 0 ? std::max(10.0f, DistVector(look, hit) - 3.0f)
                                                                                             : g_town_cam.distance);
        } else {
            const bool snap = Mutated("towncam_turn_snap");

            EdCameraWalls(camera, ground, g_town.high_camera ? 1 : 0, 0, snap ? 1 : 0, &walls);

            // coop-housecam test hook (DC_COOP_TEST_HER_TURN_ON_PEER=1): her camera turns round on link tick 2 of every link after
            // the first (his house-exit merge's catch-up, where train 51's at-once turn jumped her eye; tick 2, so her
            // step before it is on the same link), as a wall would turn it (t-p2-houseout-view: the turn must not move her eye
            // 15 in a frame). It reads only what a replay reproduces (the link tick), so the copy that replays her ticks
            // in the catch-up turns her on the same tick as the one that played them (coop-fx51c, 2026-11-05: as a once-only
            // latch his copy turned her while it was apart and never again in its replay: OUT OF SYNC at tick 60).
            // DC_COOP_MUTATE=herturn_latch (twin): the once-only latch, as before.
            static const bool turn_on_peer = TestEnv("DC_COOP_TEST_HER_TURN_ON_PEER") != nullptr;
            static bool       latch_left = true;
            static long long  tick_was = -1;
            static bool       relinked_once = false;
            const long long   tick = NetplayLinkTick();

            relinked_once = relinked_once || (tick_was >= 0 && tick < tick_was);
            tick_was = tick;

            const bool latch = Mutated("herturn_latch");

            if (turn_on_peer && relinked_once && tick == 2 && (!latch || latch_left)) {
                latch_left = false;
                walls.turned = 1;
                std::printf("coop: test: her camera turns round on link tick 2 of a new link (tick %lld)\n", tick);

                if (snap) {
                    camera->SetDistance(camera_near_dist);
                    camera->SetHeight(10.0f);
                    camera->SetAngleSoon(camera->GetAngle());
                    camera->AddAngle(3.14f);
                    TownCamFollow();
                    camera->Step(-1);
                }
            }

            if (walls.turned != 0 && !snap && g_town_cam.swing <= 0.0f) {
                g_town_cam.swing = 3.14159265f;
            }
        }

        if (static const bool trace = TestEnv("DC_COOP_TEST_CAMTRACE") != nullptr; trace && walls.turned != 0) {
            std::fprintf(stderr, "coop-wallcam: tick %lld turned round from a wall\n", (long long) NetplayLinkTick());
        }
    }

    // Her right stick, R1 and L1 turn her camera as EdMoveChara's turn Toan's, not into a wall beside the eye.
    float turn = Stick(pad.right_x);
    bool  drift = false;

    if ((turn > 0.0f && walls.left_clear != 0) || (turn < 0.0f && walls.right_clear != 0)) {
        camera->AddAngle(0.03f * -turn);
    }

    if (turn == 0.0f) {
        if (walls.left_clear != 0 && (pad.buttons & kInputR1) != 0) {
            camera->AddAngle(-DEG_TO_RAD);
        } else if (walls.right_clear != 0 && (pad.buttons & kInputL1) != 0) {
            camera->AddAngle(DEG_TO_RAD);
        } else {
            drift = true;
        }
    }

    // As EdMoveChara eases Toan's: back in toward the far distance, out toward the near one.
    if (camera->GetDistance() > camera_far_dist) {
        camera->AddDistance(-(camera->GetDistance() - camera_far_dist) / 10.0f);
    }

    if (camera->GetDistance() < camera_near_dist) {
        camera->AddDistance(-(camera->GetDistance() - camera_near_dist) / 10.0f);
    }

    // coop-xiaocam (Jep, 2026-10-26: "Xiao's camera doesn't adjust automatically while she moves like Toan's does"): with
    // the right stick, R1 and L1 at rest, her camera swings round behind her as she walks, as EdMoveChara's drift swings
    // Toan's: by the left stick's sideways share (half at least while she walks back toward it; toward the open side by a
    // wall), once her stride is long enough. Her stride is her last step's (she walks after her camera steps).
    // DC_COOP_MUTATE=towncam_no_drift (twin).
    sceVu0FVECTOR stride;
    const float   before = camera->next_angle;

    sceVu0SubVector(stride, g_town.pos, g_town_cam.last);
    stride[3] = 0.0f;
    sceVu0CopyVector(g_town_cam.last, g_town.pos);

    if (drift && !Mutated("towncam_no_drift")) {
        float lx = Stick(pad.left_x);
        float ly = Stick(pad.left_y);
        float side = lx;

        if (ly > 0.05f) {
            if (!(side < 0.0f) && side < 0.5f) {
                side = 0.5f;
            }

            if (side <= 0.0f && side > -0.5f) {
                side = -0.5f;
            }

            if (walls.left_clear == 0) {
                side = 0.5f;
            }

            if (walls.right_clear == 0) {
                side = -0.5f;
            }
        }

        side *= DistVector(stride) / 1.6f / 2.0f;

        if (side > 0.1f && walls.left_clear != 0) {
            camera->AddAngle(2.0f * (-DEG_TO_RAD * std::clamp(lx, 0.4f, 1.0f)));
        }

        if (side < -0.1f && walls.right_clear != 0) {
            camera->AddAngle(2.0f * (-DEG_TO_RAD * std::clamp(lx, -1.0f, -0.4f)));
        }
    }

    // DC_COOP_DEBUG: her camera's angle and her facing twice a second while she walks (t-p2-town-cam's -Case drift reads it).
    if (static int ticks = 0; CoopDebug() && ++ticks % 30 == 0 && DistVector(stride) > 0.0f && g_town.body != nullptr) {
        sceVu0FVECTOR rot;

        g_town.body->frame->GetRotation(rot);
        std::printf("coop-debug: p2 town camera drift angle %.3f (turned %.3f) facing %.3f stride %.2f stick %.2f %.2f\n", camera->next_angle,
                    camera->next_angle - before, rot[1], DistVector(stride), Stick(pad.left_x), Stick(pad.left_y));
    }

    if (walls.keep_distance != 0 && !Mutated("towncam_wall_from_eye")) {
        camera->SetDistance(camera_near_dist);
    }

    // The eye's height over her head as EdMoveChara keeps Toan's: the stick moves it only below 30, it stays between 5
    // (35 on a high-camera floor) and 60, and settles back toward the lowest. With no floor the stick took her eye under
    // the town's ground (y -136 by Norune's entrance), and her screen's light jumped as it went.
    // DC_COOP_MUTATE=towncam_floor keeps the old limit, 30 at most and no floor (twin).
    if (Mutated("towncam_floor")) {
        camera->AddHeight(-Stick(pad.right_y));

        if (camera->GetHeight() > 30.0f) {
            camera->SetHeight(30.0f);
        }
    } else {
        if (camera->GetHeight() < 30.0f) {
            camera->AddHeight(-Stick(pad.right_y));
        }

        const float lowest = g_town.high_camera ? 35.0f : 5.0f;

        camera->SetHeight(std::clamp(camera->GetHeight(), lowest, 60.0f));

        if (camera->GetHeight() > 5.0f) {
            camera->AddHeight(-std::clamp(0.05f * (camera->GetHeight() - lowest), 0.15f, 0.5f));
        }
    }

    if (g_town_cam.swing > 0.0f) {
        const float step = std::min(g_town_cam.swing, kSwingStep);

        camera->AddAngle(step);
        g_town_cam.swing -= step;
    }

    TownCamFollow();
    camera->Step(1);

    // coop-campulse (DC_COOP_TEST_CAMTRACE): each step her camera's distance (full, and as the walls left it), how far the
    // stepped eye is from her head, and whether a camera wall stands between them ("behind 1": t-p2-house-cam's mode).
    if (static const bool trace = TestEnv("DC_COOP_TEST_CAMTRACE") != nullptr; trace) {
        sceVu0FVECTOR stepped;
        int           behind = 0;

        camera->GetPos(stepped);
        if (!g_indoor.on && EdMoveCharaInfo.interior == 0) {
            CEditGround  *ground = EdMoveCharaInfo.ground != nullptr ? EdMoveCharaInfo.ground : pEditGround;
            CBoxVu0       box;
            sceVu0FVECTOR hit;
            const float   span = DistVector(stepped, look) + 20.0f;

            box.max[0] = look[0] + span;
            box.min[0] = look[0] - span;
            box.max[2] = look[2] + span;
            box.min[2] = look[2] - span;
            box.max[1] = look[1] + 100.0f;
            box.min[1] = look[1] - 100.0f;
            WorkBuffer->used = 0;

            CCPoly *polys = (CCPoly *) WorkBuffer->Alloc(0x7D0);
            int     count = ground->PickUpCameraPoly(polys, box, 0xFFFF);

            behind = count > 0 && CheckHit(polys, count, look, stepped, hit, 1, 0) >= 0 ? 1 : 0;
        }
        std::fprintf(stderr, "coop-wallcam: tick %lld at (%.1f %.1f %.1f) full %.1f wall %.1f eye %.2f angle %.3f height %.1f swing %.2f behind %d eye at (%.1f %.1f %.1f)\n",
                     (long long) NetplayLinkTick(), g_town.pos[0], g_town.pos[1], g_town.pos[2], camera_near_dist, camera->GetDistance(),
                     DistVector(stepped, look), camera->next_angle, camera->GetHeight(), g_town_cam.swing, behind, stepped[0], stepped[1], stepped[2]);
    }
}
} // namespace
} // namespace coop

namespace dc::game {
int EdInCameraMarkerAt(const float *pos, int eye_of, float *eye); // edit_in.cpp (coop-inbuild)
void EdInCameraRetake();                                          // edit_in.cpp (coop-doorflash)
int EdInDoorAt(const float *pos, const float *rot);
bool EdInDoorPose(float *pos, float *rot);
void EdInLeaveByDoor(int link_id);
void EdInOpenDoorBy(int link_id); // coop-housefade
int  EdInDoorOpenPose(int link_id, float *position, float *rotation, int *sound); // coop-pt1106-batch
void EdDoorOpenSe(int door_no, float *position);
void EdFadeIn(int frames, float red, float green, float blue);
void EdFadeOut(int frames, float red, float green, float blue);
void EdInDoorsLog(); // coop-p2doors
} // namespace dc::game

namespace coop {
namespace {

// coop-inbuild: her camera indoors starts from the room's (Toan's): its angle, distance and height.
void IndoorCamInit() {
    if (g_town_cam.camera == nullptr) {
        g_town_cam.camera = new CCameraFollow(60.0f, 20.0f, 0.0f, 4.0f);
    }

    CCameraFollow *camera = g_town_cam.camera;
    CCameraFollow *room = g_indoor.room;

    camera->SetSpeed(7.0f);
    camera->FollowOn();
    camera->SetAngleSoon(room->GetAngle());
    camera->SetDistance(room->GetDistance());
    camera->SetHeight(room->GetHeight());
    g_town_cam.distance = room->GetDistance();
    g_town_cam.swing = 0.0f;
    g_indoor.marker = -1;
    g_indoor.marker_hold = 0;
    TownCamFollow();
    camera->Step(-1);
    g_town_cam.ready = true;
    g_town_cam.map = MapNo;
    g_town_cam.indoor = true;
}

// Her camera indoors, as the room's is Toan's: in a room with fixed camera markers, the eye of the marker whose box she
// stands in (held 20 ticks before it changes, as SetCameraPos holds his) looking at her; else it follows her, turned by
// her right stick, R1 and L1. Both copies step it alike from pad 2's shared pad; her copy draws from it.
// DC_COOP_MUTATE=indoor_cam_toan (twin): her camera keeps the marker Toan's is at.
void IndoorCamStep(const InputPadState &pad) {
    CCameraFollow *camera = g_town_cam.camera;
    const float   *who = g_town.pos;
    sceVu0FVECTOR  toan;

    if (Mutated("indoor_cam_toan") && Chara != nullptr) {
        Chara->GetPosition(toan);
        who = toan;
    }

    // (opt8 test hook DC_COOP_TEST_ROOM_FOLLOW_CAM=1, w-indoor -FollowCam: the room's camera markers left out, her camera on its follow branch as
    // in a room without markers; the mayor's house, the room talk rows' room, has them)
    static const bool follow_only = TestEnvOn("DC_COOP_TEST_ROOM_FOLLOW_CAM");
    const int         inside = g_indoor.markers && !follow_only ? EdInCameraMarkerAt(who, -1, nullptr) : -1;

    if (follow_only) {
        g_indoor.marker = -1;
    }

    if (inside >= 0) {
        if (g_indoor.marker < 0 || g_indoor.marker_hold <= 0) {
            g_indoor.marker = inside;
            g_indoor.marker_hold = 0;
        }

        if (inside == g_indoor.marker) {
            g_indoor.marker_hold = 20;
        } else {
            g_indoor.marker_hold--;
        }
    }

    sceVu0FVECTOR eye;

    camera->GetPos(eye);

    if (g_indoor.marker >= 0) {
        EdInCameraMarkerAt(who, g_indoor.marker, eye);

        sceVu0FVECTOR look = {g_town.pos[0], g_town.pos[1] + 17.0f, g_town.pos[2], 1.0f};

        camera->FollowOff();
        camera->SetPos(eye);
        camera->SetRef(look);
        camera->SetNextPos(nullptr, eye[0], eye[1], eye[2]);
        camera->SetNextRef(nullptr, look[0], look[1], look[2]);
    } else {
        const float turn = Stick(pad.right_x);

        camera->FollowOn();
        camera->AddAngle(0.03f * -turn);

        if (turn == 0.0f) {
            if ((pad.buttons & kInputR1) != 0) {
                camera->AddAngle(-DEG_TO_RAD);
            }

            if ((pad.buttons & kInputL1) != 0) {
                camera->AddAngle(DEG_TO_RAD);
            }
        }

        TownCamFollow();
    }

    camera->Step(1);
}

SpawnStand g_room_stand; // coop-localfix: her stand from a merge inside this room (TownMergedIndoors), for PlaceInRoom

// coop-inbuild: indoors she starts a step into the room before Toan, who stands where the door brought him, facing in.
void PlaceInRoom() {
    sceVu0FVECTOR at;
    sceVu0FVECTOR rotation;
    constexpr float kAhead = 12.0f;

    Chara->GetPosition(at);
    Chara->GetRotation(rotation);

    // coop-localfix: a merge inside (she came in by the door while he was in the room): where her own copy had her, the
    // host's stand on both copies, not beside Toan.
    if (g_room_stand.set) {
        g_room_stand.set = false;
        g_town.pos[0] = g_room_stand.pos[0];
        g_town.pos[1] = g_room_stand.pos[1];
        g_town.pos[2] = g_room_stand.pos[2];
        g_town.pos[3] = 1.0f;

        if (SpawnOccupied(at, g_town.pos)) {
            float spot[3];

            TownSecondSpot(g_town.pos, g_room_stand.yaw, at, spot);
            g_town.pos[0] = spot[0];
            g_town.pos[1] = spot[1];
            g_town.pos[2] = spot[2];
        }

        g_town.velocity[0] = g_town.velocity[1] = g_town.velocity[2] = 0.0f;
        g_town.body->frame->SetPosition(g_town.pos);
        g_town.body->frame->SetRotation(0.0f, g_room_stand.yaw, 0.0f);
        g_town.body->SetPosition(g_town.pos);
        g_town.body->SetRotation(0.0f, g_room_stand.yaw, 0.0f);
        g_indoor.ticks = 0;
        std::printf("coop: P2 placed in the room %s where she stood (the merge) at (%.0f %.0f %.0f), Toan at (%.0f %.0f %.0f)\n", EdInteriorName,
                    g_town.pos[0], g_town.pos[1], g_town.pos[2], at[0], at[1], at[2]);
        return;
    }

    g_town.pos[0] = at[0] + kAhead * sinf(rotation[1]);
    g_town.pos[1] = at[1];
    g_town.pos[2] = at[2] + kAhead * cosf(rotation[1]);
    g_town.pos[3] = 1.0f;
    g_town.velocity[0] = g_town.velocity[1] = g_town.velocity[2] = 0.0f;
    g_town.body->frame->SetPosition(g_town.pos);
    g_town.body->frame->SetRotation(0.0f, rotation[1], 0.0f);
    g_town.body->SetPosition(g_town.pos);
    g_town.body->SetRotation(0.0f, rotation[1], 0.0f);
    g_indoor.ticks = 0;
    std::printf("coop: P2 placed in the room %s at (%.0f %.0f %.0f), Toan at (%.0f %.0f %.0f)\n", EdInteriorName, g_town.pos[0], g_town.pos[1],
                g_town.pos[2], at[0], at[1], at[2]);

    if (CoopDebug()) {
        EdInDoorsLog(); // coop-p2doors
    }
}

// DC_COOP_DEBUG: indoors once a second, both players and her camera (the inbuild tests read it).
void IndoorDebugLog() {
    static const bool enabled = CoopDebug();
    static int        wait = 0;

    if (!enabled || --wait > 0) {
        return;
    }

    wait = 60;

    sceVu0FVECTOR toan;
    sceVu0FVECTOR eye = {0.0f, 0.0f, 0.0f, 0.0f};

    Chara->GetPosition(toan);

    if (g_town_cam.ready) {
        g_town_cam.camera->GetPos(eye);
    }

    InputPadState routed = Pad2();
    InputPadState synced = {};

    NetplayPlayerPad(2, synced);
    std::printf("coop-debug: drawn eye (%.0f %.0f %.0f) ref (%.0f %.0f %.0f) from %s\n", g_drawn_eye[0], g_drawn_eye[1], g_drawn_eye[2], g_drawn_ref[0], g_drawn_ref[1], g_drawn_ref[2], g_town_view.own ? "her camera" : "the game's");
    std::printf("coop-debug: indoors %s p1 (%.1f %.1f %.1f) p2 (%.1f %.1f %.1f) motion %d stick (%.2f %.2f) pad %d %d synced %d %d room mode %d marker %d eye (%.0f %.0f %.0f)\n",
                EdInteriorName, toan[0], toan[1], toan[2], g_town.pos[0], g_town.pos[1], g_town.pos[2], g_town.motion, g_indoor.stick[0], g_indoor.stick[1],
                routed.left_x, routed.left_y, synced.left_x, synced.left_y, g_indoor.mode, g_indoor.marker, eye[0], eye[1], eye[2]);
}

// Test hook (DC_COOP_TOWN_WARP=tick:x:z[:x2:z2]): on the tick-th walking tick after the partner's last (re)load (a resync
// reloads the town on both copies, so a late joiner counts from the same tick as the host), Toan to (x, z) and the partner beside
// him (or to (x2, z2)), both a little above the ground, as a long run there would leave them. Both linked copies read
// the same value, so it is the same move on both.
void TownWarpForTest() {
    // DC_COOP_TOWN_CLOCK=tick:time: on that tick (the same count), the town clock to time (0 to 12; a period changes at
    // 2.5, 5.5, 8.5 and 11.5), so a test reaches a change of the time of day in seconds.
    // coop-warmconvert (WS7, t-geoview): the three switches are a job's own on a warm pair (COOP_TEST_SWITCH).
    COOP_TEST_SWITCH(const char *, clock, TestEnv("DC_COOP_TOWN_CLOCK"));
    int tick = ++g_town_warp_ticks;
    int                clock_at = 0;
    float              time = 0.0f;
    // coop-georamafix: DC_COOP_TOWN_WARP_PRESS=1: the warps' ticks on the press clock (DC_COOP_MENUPRESS's, alike on both copies
    // after a fixture's load, which this count is not: it runs from each copy's own partner load), -1 before it starts.
    // coop-fx51c: the clock's tick too (n-time-event -Fixture: dusk on the fixture, both copies on one tick).
    COOP_TEST_SWITCH(bool, on_press, TestEnvOn("DC_COOP_TOWN_WARP_PRESS"));

    if (on_press) {
        tick = (int) NetplayTestPressClock();
    }

    if (clock != nullptr && std::sscanf(clock, "%d:%f", &clock_at, &time) == 2 && tick == clock_at) {
        NowTime = time;
        SaveData->SetNowTime(time);
        std::printf("coop: town clock test at tick %d: clock to %.3f\n", clock_at, time);
    }

    COOP_TEST_SWITCH(const char *, spec, TestEnv("DC_COOP_TOWN_WARP"));

    int               at = 0;
    float              x = 0.0f, z = 0.0f, x2 = 0.0f, z2 = 0.0f;

    if (spec == nullptr) {
        return;
    }

    // coop-georama-pt: more warps after the first, ';' between them (t-p2-georama-on -Case p2-beside-toan: Toan onto the part
    // she put down); a later one with no x2:z2 moves Toan alone.
    const char *entry = spec;
    int         index = 0;
    int         fields = 0;

    for (; entry != nullptr; index++) {
        fields = std::sscanf(entry, "%d:%f:%f:%f:%f", &at, &x, &z, &x2, &z2);

        if (fields >= 3 && tick == at) {
            break;
        }

        entry = std::strchr(entry, ';');
        entry = entry != nullptr ? entry + 1 : nullptr;
    }

    if (entry == nullptr) {
        return;
    }

    if (index > 0 && fields < 5) {
        sceVu0FVECTOR toan_to = {x, 20.0f, z, 1.0f};

        Chara->SetPosition(toan_to);
        Chara->frame->SetPosition(toan_to);
        Chara->Step();
        std::printf("coop: town warp test at tick %d: Toan alone to (%.0f %.0f)\n", at, x, z);
        return;
    }

    sceVu0FVECTOR to = {x, 20.0f, z, 1.0f};

    Chara->SetPosition(to);
    Chara->frame->SetPosition(to);
    Chara->Step();

    if (fields >= 5) {
        sceVu0FVECTOR p2 = {x2, 20.0f, z2, 1.0f};

        sceVu0CopyVector(g_town.pos, p2);
        g_town.velocity[0] = g_town.velocity[1] = g_town.velocity[2] = 0.0f;
        g_town.has_ground = false;
        g_town.airborne = 0;
    } else {
        if (g_town.body != nullptr) {
            PlaceBesideToan("warp test");
        }
    }

    std::printf("coop: town warp test at tick %d: Toan to (%.0f %.0f)\n", at, x, z);
}

} // namespace

// coop-camjump: the game camera's walk steps since the split handed it her eye (CoopTownStep reports its first), and that eye.
namespace {
int           g_split_cam_check = 0;
sceVu0FVECTOR g_split_cam_eye = {};
} // namespace

// coop-worlds: player 2's copy staying in town: Toan, the town's player, stands where the partner stood.
void TownCamPose(CCameraFollow &to); // (below)

bool TownPlacePlayerAtPartner(float *out) {
    if (Chara == nullptr || Chara->frame == nullptr || g_town.loaded < 0) {
        return false;
    }

    Chara->SetPosition(g_town.pos);
    Chara->frame->SetPosition(g_town.pos);

    // coop-localfix (Jep's 2026-10-26 retests: "P2 still turns and faces towards the house, and the camera still looks in
    // that direction" as P1 goes in): her facing too, not Toan's (he faced his door), and her skin and camera follow it.
    // DC_COOP_MUTATE=split_faces_his (twin): Toan's facing, as before.
    if (g_town.body != nullptr && g_town.body->frame != nullptr) {
        sceVu0FVECTOR rotation;
        sceVu0FVECTOR his;

        g_town.body->frame->GetRotation(rotation);
        Chara->frame->GetRotation(his);
        if (!Mutated("split_faces_his")) {
            Chara->SetRotation(0.0f, rotation[1], 0.0f);
            Chara->frame->SetRotation(0.0f, rotation[1], 0.0f);
        }

        sceVu0FVECTOR now;

        Chara->frame->GetRotation(now);
        std::printf("coop: her facing at the split %.2f (hers %.2f, Toan's %.2f)\n", now[1], rotation[1], his[1]);
    }
    Chara->Step();

    // coop-localfix (Jep's 2026-10-26 retests: "P2s camera still jumps when P1 enters the house"): the game camera on her copy, his
    // until here, takes her camera's pose, so her screen does not swing from his door to her. Apart, it is hers alone.
    // DC_COOP_MUTATE=split_cam_his (twin): it follows her from his, as before.
    if (g_town_cam.ready && !Mutated("split_cam_his")) {
        TownCamPose(MainCamera);
        // coop-camjerk2 (Jep, 2026-10-27: her camera still jumped as Toan went into the mayor's house while she stood in town):
        // and the point it circles, its distance and its height, hers too. Left at his door, the game camera's next step
        // pulled her eye some 90 toward it in one frame, and it slid back to her over the next 50.
        // DC_COOP_MUTATE=split_follow_his (twin): the pose alone, as before.
        if (!Mutated("split_follow_his")) {
            CCameraFollow *hers = g_town_cam.camera;

            MainCamera.SetFollow(hers->follow[0], hers->follow[1], hers->follow[2]);
            // coop-camjump (onher-hostsends, 2026-10-27: her view turned 164 degrees in a frame as he went in, she on the porch):
            // the circle of the eye on her screen, measured from the point it circles, not her camera's targets. Her wall rule
            // keeps her distance 3 nearer than a wall and her eye eases onto it, so beside the porch her target was 34.3 while
            // her eye stood 37.6 off. The game camera's first walk step reads a distance under half its near one (35) beside
            // a wall as boxed in and turns round to the far side at once (EdMoveChara: distance 70, height 10, half a turn).
            // From her eye it has no step to take, and its own wall rule measures the wall where it is.
            // DC_COOP_MUTATE=split_cam_targets (twin): her camera's distance, height and angle, as before.
            sceVu0FVECTOR eye;

            hers->GetPos(eye);
            const float dx = eye[0] - hers->follow[0];
            const float dz = eye[2] - hers->follow[2];

            if (Mutated("split_cam_targets")) {
                MainCamera.SetDistance(hers->distance);
                MainCamera.SetHeight(hers->height);
            } else {
                MainCamera.SetDistance(std::sqrt(dx * dx + dz * dz));
                MainCamera.SetHeight(eye[1] - hers->follow[1]);
                MainCamera.SetAngleSoon(std::atan2(dx, dz));
            }

            g_split_cam_check = 1;
            sceVu0CopyVector(g_split_cam_eye, eye);
            std::fprintf(stderr, "coop-town: the split hands the game camera distance %.1f height %.1f (her eye %.1f off, her camera's target %.1f)\n",
                         MainCamera.distance, MainCamera.height, std::sqrt(dx * dx + dz * dz), hers->distance);
        }

        if (TestEnv("DC_COOP_TEST_CAMTRACE") != nullptr) {
            std::fprintf(stderr, "coop-splittrace: the split's pose: game camera dist %.1f h %.1f angle %.2f follow (%.1f %.1f %.1f)\n",
                         MainCamera.distance, MainCamera.height, MainCamera.angle, MainCamera.follow[0], MainCamera.follow[1], MainCamera.follow[2]);
        }
    }

    // coop-doorflash (Jep, 2026-10-27, both in the mayor's house: "the moment P1 opens the door to leave, P2's screen shows
    // something other than her view for about one frame"): inside a building her screen goes to the room's camera once the
    // link is down, not the town's, and the room's was his: posed on his marker at the door, looking at Toan there. It takes
    // her camera's state, and its marker hold lets go, so its next step takes the marker of the box she stands in.
    // DC_COOP_MUTATE=split_room_cam_his (twin): the room's camera left his, as before.
    if (g_town_cam.ready && g_town_cam.indoor && EdInteriorFlag != 0 && g_indoor.room != nullptr && !Mutated("split_room_cam_his")) {
        constexpr std::size_t from = offsetof(CCameraFollow, pos);

        std::memcpy(reinterpret_cast<unsigned char *>(g_indoor.room) + from, reinterpret_cast<const unsigned char *>(g_town_cam.camera) + from,
                    sizeof(CCameraFollow) - from);
        EdInCameraRetake();
        std::printf("coop: the room's camera takes hers at the split\n");
    }

    for (int i = 0; i < 3; i++) {
        out[i] = g_town.pos[i];
    }

    return true;
}

// coop-town: player 2's own Georama starts from her camera (its angle, eye and look), else Toan's.
void TownCamPose(CCameraFollow &to) {
    CCameraFollow *from = g_town_cam.ready ? g_town_cam.camera : &MainCamera;
    sceVu0FVECTOR  at;

    to.SetAngleSoon(from->GetAngle());
    from->GetPos(at);
    to.SetPos(at);
    from->GetRef(at);
    to.SetRef(at);
}

// coop-town: player 2 leaves her Georama where its cursor stood; her camera follows her there.
void TownPlacePartner(const float *at) {
    for (int i = 0; i < 3; i++) {
        g_town.pos[i] = at[i];
        g_town.velocity[i] = 0.0f;
    }

    g_town.has_ground = false;
    g_town.airborne = 0;

    if (g_town_cam.ready) {
        TownCamFollow();
        g_town_cam.camera->Step(-1);
    }
}

// coop-worlds: player 2's copy while apart plays her own member. The game's player there is Toan (the
// town has only him), so he walks unseen and her model is drawn where he is, in his place. Drawing only.
bool g_town_skin = false;
bool g_town_skin_said = false; // its log line, once per time apart

void TownSkinStep() {
    int chara = TownMember();

    if (g_town.loaded != chara || g_town.map != MapNo || BlockEmptyNow()) {
        if (!TownLoad(chara)) {
            g_town.loaded = -1;
            return;
        }
    }

    if (!g_town_skin_said) {
        g_town_skin_said = true;
        std::printf("coop: player 2 apart in town %d: %s drawn in Toan's place\n", MapNo, charaNameTbl[chara]);
    }

    CCharacter   *body = g_town.body;
    sceVu0FVECTOR rotation;
    sceVu0FVECTOR was;

    sceVu0CopyVector(was, g_town.pos);
    Chara->GetPosition(g_town.pos);
    Chara->frame->GetRotation(rotation);
    body->motion_type.motion_info->speed = 0.05f;

    // Her walk from how far Toan moved this tick, whatever moved him (a pad, the keyboard, a script): full tilt at the
    // partner's full speed, none for a jump (a warp, her first tick) or a drift. The first pad's stick alone missed the keyboard,
    // so she slid about standing (Jep, 2026-10-24). DC_COOP_MUTATE=skin_pad (twin): the first pad's stick, as before.
    const float   step = std::hypot(g_town.pos[0] - was[0], g_town.pos[2] - was[2]);
    const float   full = EdMoveCharaInfo.interior != 0 ? 1.0f : 1.6f;
    float         tilt = step < 0.1f || step > 8.0f ? 0.0f : std::min(1.0f, step / full);
    float         lx = 0.0f;

    if (Mutated("skin_pad")) {
        InputPadState pad = InputGetPad(0);

        lx = Stick(pad.left_x);
        tilt = Stick(pad.left_y);
    }

    int motion = keyCtrl(lx, tilt, body->motion_type.motion_info);

    if (static int ticks = 0; CoopDebug() && ++ticks % 60 == 0) {
        std::fprintf(stderr, "coop-debug: her skin at (%.0f %.0f): step %.2f, motion %d\n", g_town.pos[0], g_town.pos[2], step, motion);
    }

    if (motion != g_town.motion) {
        body->motion_no = motion;
        body->motion_flags = 0;
        body->motion_speed = -1.0f;
        g_town.motion = motion;
    }

    body->frame->SetPosition(g_town.pos);
    body->frame->SetRotation(0.0f, rotation[1], 0.0f);
    body->SetPosition(g_town.pos);
    body->SetRotation(0.0f, rotation[1], 0.0f);
    // coop-footsteps (Jep's 2026-10-28 online run: P1 in the mayor's house, P2 outside heard her steps "plus constant wood
    // footsteps"): her steps are Chara's, by the ground EdMoveChara stands him on; the skin over him is only drawn. Its own
    // steps (on since coop-doorfade) sounded too, with the foot sound id of its last landing as the partner (the room's wood)
    // or its load. PartnerStep turns them on again. DC_COOP_MUTATE=skin_steps (twin): the skin's steps sound, as before.
    body->FootSoundEnable(Mutated("skin_steps") ? 1 : 0);
    body->Step();
    FootstepNote(body, "skin");
    FootstepNote(Chara, "own");
    body->ShadowStep();
    body->ClothStep(0);
    g_town_skin = true;
    g_town.visible = true;
}

void TownMergedOutdoors() {
    // coop-localfix (Jep's 2026-10-26 retest, note 8: back from the dungeon "it did put them on top of each other causing a
    // reinstance"): on the merge's tick, on both copies alike, she stands where her copy had her, a step aside when Toan stands
    // there (the host's town load, which only his copy has, keeps it). DC_COOP_MUTATE=merge_place_at_load (twin): left to
    // the host's load, as before.
    SpawnStand stand;

    // coop-pt1108b (Jep's rule, 11-08: a joiner is hidden through a scene, shown at her arrival spot as it ends): nothing steps her through a town event, so whether she
    // was shown stayed each copy's own from before the merge (his yes, hers no): OUT OF SYNC on the next check inside it. Hidden here on both.
    // DC_COOP_MUTATE=scene_arriver_own (twin): left as each copy had it.
    if (::GameMode == ED_MODE_EVENT && !Mutated("scene_arriver_own")) {
        g_town.visible = false;
    }

    // coop-herexit2 (Jep, 2026-10-27: "P2 leaving the house after P1 is causing P2's screen to go dark and lose input"): their
    // last step together was in a room (he went out first, she after), so her camera on both copies was still the room's,
    // some 650 away in the town (her screen a field with her nowhere in it), and his copy, its last step of her indoors,
    // put her "beside Toan (out of a building)" while hers kept her at the merge's place. She is outdoors now on both:
    // her side of the door this one, and her camera made afresh here from the town's on both copies.
    // DC_COOP_MUTATE=merge_out_room_cam (twin): left as they were.
    bool dropped = false; // coop-housecam

    if (!Mutated("merge_out_room_cam")) {
        g_indoor.was = false;
        if (g_town_cam.ready && g_town_cam.indoor) {
            g_town_cam.ready = false;
            dropped = true;
            std::printf("coop: her room camera dropped at the town merge (made afresh from the town's)\n");
        }
    }

    g_merge_place.set = false;
    if (Mutated("merge_place_at_load") || Chara == nullptr || !SpawnPeekStand(MapNo, &stand)) {
        return;
    }

    sceVu0FVECTOR toan;

    Chara->GetPosition(toan);
    if (SpawnOccupied(toan, stand.pos)) {
        float spot[3];

        TownSecondSpot(stand.pos, stand.yaw, toan, spot);
        stand.pos[0] = spot[0];
        stand.pos[1] = spot[1];
        stand.pos[2] = spot[2];
    }

    g_merge_place = stand;
    g_town.pos[0] = stand.pos[0];
    g_town.pos[1] = stand.pos[1];
    g_town.pos[2] = stand.pos[2];
    g_town.pos[3] = 1.0f;
    g_town.velocity[0] = g_town.velocity[1] = g_town.velocity[2] = 0.0f;

    if (g_town.body != nullptr && g_town.body->frame != nullptr) {
        g_town.body->frame->SetPosition(g_town.pos);
        g_town.body->frame->SetRotation(0.0f, stand.yaw, 0.0f);
        g_town.body->SetPosition(g_town.pos);
        g_town.body->SetRotation(0.0f, stand.yaw, 0.0f);
    }

    std::printf("coop: P2 placed at the merge at (%.0f %.0f %.0f), Toan at (%.0f %.0f %.0f)\n", g_town.pos[0], g_town.pos[1], g_town.pos[2], toan[0], toan[1],
                toan[2]);

    // coop-housecam (Jep's 2026-10-28 online playtest: "when P2 left the mayor's house after P1, their camera spawned inside of
    // the mayor's house"): her room camera dropped above, her camera made afresh from the game's camera took its angle, and
    // after the merge's state set that is Toan's, wherever he walks in the town: at her door, an angle from the house's side put
    // her eye inside the house. Made here on both copies as the game's camera shows a player coming out of a door: out on the
    // side she faces (her stand's yaw: she faces away from the door she came out of), looking back at her and the door.
    // (Behind her facing, yaw - pi, is inside the house: tried, l-local-play -HerOutSecond's frames show the house's wall.)
    // DC_COOP_MUTATE=merge_cam_his_angle (twin): his angle, as before.
    if (dropped && !Mutated("merge_cam_his_angle")) {
        float yaw = stand.yaw;

        while (yaw > PI_SHORT) {
            yaw -= TWO_PI;
        }
        while (yaw <= -PI_SHORT) {
            yaw += TWO_PI;
        }
        TownCamInit();
        g_town_cam.camera->SetAngleSoon(yaw);
        g_town_cam.camera->Step(-1);
        std::fprintf(stderr, "coop: her camera at the town merge out of a room: out on the side she faces (angle %.2f, her yaw %.2f) at (%.0f %.0f %.0f)\n", yaw, stand.yaw,
                     g_town.pos[0], g_town.pos[1], g_town.pos[2]);
        std::fflush(stderr);
    }
}

// coop-exitscene (Jep's 2026-10-27 run, run log 2026-10-27c: back from the dungeon after P1, "Xiao stood in front of the
// mayor's house door instead of in front of the dungeon"): a merge's place is for the host's town load of that merge alone. A
// merge with no load after it (a catch-up: the host plays on) left it set; the pair split, and his next "first" load of her,
// as her later merge began, put her there (where an earlier merge had her) and used up her stand, so the merge's own placing
// had none and his state set sent her to the old place. Dropped at every split. DC_COOP_MUTATE=merge_place_kept (twin): kept.
void TownMergePlaceDrop() {
    if (!Mutated("merge_place_kept")) {
        g_merge_place.set = false;
    }
}

void TownMergedIndoors() {
    // Apart inside, her step was off and marked her placed (CoopTownInteriorStep): the copy that stayed had her where
    // she last stood outside, the copy that came in beside Toan as her body loaded (house merge, 2026-10-25: OUT OF SYNC
    // 50 ticks on). DC_COOP_MUTATE=merge_indoor_stale (twin): each copy keeps its own.
    if (!Mutated("merge_indoor_stale")) {
        g_indoor.was = false;
    }

    // coop-localfix: and where her copy had her in the room (the merge's stand, set alike on both copies), for her placing.
    g_room_stand.set = false;
    SpawnTakeStand(MapNo | kIndoorStand, &g_room_stand);
}

void TownResetForSession() {
    g_town_cam.ready = false;
    g_town_cam.map = -1;
    g_town.loaded = -1;
    g_town.map = -1;
    g_town.visible = false;
    g_town.motion = -1;
    g_town.airborne = 0;
    g_town.has_ground = false;
    g_indoor.was = false; // coop-inbuild
    g_indoor.marker = -1;
    // opt8 (the resync texture flood): her room event and every talk slot end too. A resync takes the host's state and loads the zone
    // again, which ends retail's own talk (its state is the zone's) and makes the message windows and the talk script anew; a talk kept
    // here drew its window from the old load's texture every frame ("gfx: a recorded draw names unavailable texture 0x1", about 100
    // times a tick on both copies, with her talk's state pointing into the old zone). DC_COOP_MUTATE=talk_kept_over_reset (twin): kept, as before.
    if (!Mutated("talk_kept_over_reset")) {
        if (g_room_event.on || CoopTalkAny()) {
            std::fprintf(stderr, "coop-talk: the session reset ends every talk and player 2's room event (the zone loads again)\n");
        }
        g_room_event.on = false;
        g_room_event.villager = -1;
        g_room_event.held_up = false;
        g_room_event.pad_in = false;
        g_room_event.bag_in = false;
        std::memset(static_cast<void *>(g_talks), 0, sizeof g_talks);
    }
}

} // namespace coop

using namespace coop;

// coop-worlds: the ground kind under player 2's partner as it last landed, and the event it stepped onto (taken once).
int g_partner_ground_kind = 0;
int g_partner_ground_event = 0;

// coop-nopush (Jep's 2026-10-27 playtest: "P1 can push P2 around, while P1 is solid to P2"; his rule: neither pushes the
// other): her body as a wall in Toan's walk (EdMoveChara, after the villagers'), as a villager's is: he stops at her. Only
// while he moves (standing still, the wall's MoveCheck would step him off her as she walks up), and only while her last
// step stood her in his town or room with him (Town::stood: indoors CoopTownInteriorStep clears visible before his walk):
// not on her own copy apart, where Toan is her (her skin). DC_COOP_MUTATE=push_through
// (twin): no wall, as before.
int CoopTownPartnerWall(float *pos, const float *velocity, CCPoly *out) {
    if (!Enabled() || !g_town.stood || g_town_skin || g_town.body == nullptr || g_town.body->frame == nullptr || (CoopWorldsApart() && !NetplayStayerMerge()) ||
        Mutated("push_through") || (velocity[0] == 0.0f && velocity[2] == 0.0f)) {
        return 0;
    }

    return g_town.body->PickUpPoly(pos, out);
}

// coop-nopush test hook (DC_COOP_TOAN_WALK_INTO=K): indoors, once K of her steps out of the room's events have passed since she
// was placed in it (as DC_COOP_INDOOR_WALK_INTO counts), for 360 steps Toan's stick (as EdMoveChara reads it, on both copies
// alike) points him at her, as a walk into her by hand (after a second's walk away from her). Every 30 steps (stderr): his place and hers, the flat distance, the
// nearest he has come, and her body's width.
void CoopTownToanWalkIntoForTest(const float *pos, float angle, float *lx, float *ly) {
    static const int at = TestEnvInt("DC_COOP_TOAN_WALK_INTO", -1);
    static float     nearest = 0.0f;

    // coop-roomevent test hook (DC_COOP_TEST_ROOMEVENT_TOAN_WALK=1): through her room event his stick walks him away from her
    // for a second, then back towards her, by turns (both copies alike: her event is the same on both).
    if (static const bool walk = TestEnvOn("DC_COOP_TEST_ROOMEVENT_TOAN_WALK"); walk && g_room_event.on && !g_room_event.his_talk_placed && g_town.body != nullptr) {
        sceVu0FVECTOR her;

        g_town.body->GetPosition(her);
        const float dx = her[0] - pos[0];
        const float dz = her[2] - pos[2];
        const float flat = std::hypot(dx, dz);
        const float away = (NetplayLinkTick() - g_room_event.tick) / 60 % 2 == 0 ? -1.0f : 1.0f;

        if (flat > 0.5f) {
            const float mx = away * dx / flat;
            const float mz = away * dz / flat;

            *lx = mx * cosf(angle) - mz * sinf(angle);
            *ly = mx * sinf(angle) + mz * cosf(angle);
        }
        return;
    }

    if (at < 0 || !g_indoor.on || !g_town.stood || g_town.body == nullptr || g_indoor.ticks < at || g_indoor.ticks >= at + 360) {
        return;
    }

    const int   steps = g_indoor.ticks - at;
    const float dx = g_town.pos[0] - pos[0];
    const float dz = g_town.pos[2] - pos[2];
    const float flat = std::hypot(dx, dz);

    nearest = steps <= 60 ? flat : std::min(nearest, flat);
    if (steps % 30 == 0) {
        std::fprintf(stderr, "coop: test: Toan walks into her at step %d: he at (%.1f %.1f %.1f), she at (%.1f %.1f %.1f), %.1f flat, nearest %.1f, her width %.1f\n", steps,
                     pos[0], pos[1], pos[2], g_town.pos[0], g_town.pos[1], g_town.pos[2], flat, nearest, g_town.body->body_width);
    }

    if (flat < 0.5f) {
        *lx = *ly = 0.0f;
        return;
    }

    const float away = steps < 60 ? -1.0f : 1.0f; // first a second's walk away from her, for a run-up
    const float mx = away * dx / flat;
    const float mz = away * dz / flat;

    *lx = mx * cosf(angle) - mz * sinf(angle);
    *ly = mx * sinf(angle) + mz * cosf(angle);
}

bool CoopTownPartnerPose(float *pos, float *rot) {
    if (!g_town.visible || g_town.body == nullptr || g_town.body->frame == nullptr) {
        return false;
    }

    sceVu0FVECTOR rotation;

    g_town.body->frame->GetRotation(rotation);
    for (int i = 0; i < 3; i++) {
        pos[i] = g_town.pos[i];
        rot[i] = rotation[i];
    }

    pos[3] = rot[3] = 1.0f;
    return true;
}

// coop-mergestand: where both stand on this copy now, for a resync's state (the caller checks the town mode): false indoors
// or without Toan's body loaded; partner_set when the town partner is shown (her spot and facing), else only Toan's.
// coop-townstand: spot 2 beside spot as the town picks it (TownSecondSpot: on her ground, short of walls and off event
// ground), for the host's join stand.
void CoopTownSecondSpot(const float *spot, float yaw, const float *other, float *out) {
    TownSecondSpot(spot, yaw, other, out);
}

bool CoopTownStandsForResync(float *toan_pos, float *toan_yaw, bool *partner_set, float *partner_pos, float *partner_yaw) {
    if (g_indoor.on || Chara == nullptr || Chara->frame == nullptr) {
        return false;
    }

    sceVu0FVECTOR at;
    sceVu0FVECTOR rotation;
    float         pos[4];
    float         rot[4];

    Chara->GetPosition(at);
    Chara->GetRotation(rotation);
    toan_pos[0] = at[0];
    toan_pos[1] = at[1];
    toan_pos[2] = at[2];
    *toan_yaw = rotation[1];
    *partner_set = CoopTownPartnerPose(pos, rot);

    if (*partner_set) {
        partner_pos[0] = pos[0];
        partner_pos[1] = pos[1];
        partner_pos[2] = pos[2];
        *partner_yaw = rot[1];
    }

    return true;
}

void CoopTownPlacePartnerForTest(const float *pos) {
    sceVu0FVECTOR to = {pos[0], pos[1], pos[2], 1.0f};

    // pos[3]: her facing (yaw), as the test gives it.
    if (g_town.body != nullptr && g_town.body->frame != nullptr) {
        g_town.body->frame->SetRotation(0.0f, pos[3], 0.0f);
        g_town.body->SetRotation(0.0f, pos[3], 0.0f);
    }

    sceVu0CopyVector(g_town.pos, to);
    g_town.velocity[0] = g_town.velocity[1] = g_town.velocity[2] = 0.0f;
    g_town.has_ground = false;
    g_town.airborne = 0;
    sceVu0FVECTOR now = {0.0f, 0.0f, 0.0f, 0.0f};
    if (g_town.body != nullptr && g_town.body->frame != nullptr) {
        g_town.body->frame->GetRotation(now);
    }
    std::printf("coop: test: partner to (%.0f %.0f %.0f) facing %.2f (asked %.2f, body %d)\n", pos[0], pos[1], pos[2], now[1], pos[3], g_town.body != nullptr);
}

int CoopTownPartnerGroundEvent() {
    const int event = g_partner_ground_event;

    g_partner_ground_event = 0;
    // coop-soakfix3: nothing she stood on before her stand after a town event (her body held on its gate's ground through his walk-out)
    // starts an event: the town's first walk took it before her placement there (link tick 40 against 41). DC_NETPLAY_MUTATE=
    // her_stand_in_event (twin) as for the stand.
    if (g_stand_after_event && !NetplayMutated("her_stand_in_event")) {
        return 0;
    }
    return event;
}

// coop-soakfix3 (tests): the town event of the ground under at (0 none, -1 no ground), for the take's gate line (netplay.cpp).
int CoopTownGroundEventAt(const float *at) {
    float ground = 0.0f;
    int   kind = 0;

    return TownGroundAt(at[0], at[1], at[2], &ground, &kind) ? kind : -1;
}

// coop-p2skin: Jep's rule (2026-10-24): Xiao is always unlocked for player 2. A linked game's save (game.netplay set, host
// alone included) has her in the party from the start, as her joining does: the party of two and her wooden slingshot,
// equipped. Saved state, so both copies set it on the same town tick (a catch-up replay too) and it is in every save after.
// DC_COOP_MUTATE=xiao_locked (twin): a party of one stays one (Xiao only a stand-in, as before).
void XiaoAlwaysJoined() {
    if (!Enabled() || !NetplayConfigured() || SaveData == nullptr || Mutated("xiao_locked")) {
        return;
    }

    CDngStatusData *status = SaveData->GetDngStatus();

    if (status == nullptr || status->party_size >= 2) {
        if (static bool said = false; status != nullptr && !said) {
            said = true;
            std::fprintf(stderr, "coop: Xiao is in the party already (party %d)\n", status->party_size);
        }

        return;
    }

    const int was = status->party_size;

    status->SetPartySize(2);

    if (status->equipped_weapon_slot[CHARA_XIAO] < 0 && status->chara_weapons[CHARA_XIAO][0].item_no < 0) {
        status->GetItem(ITEM_WEAPON_WOODEN_SLINGSHOT, 0);
        status->equipped_weapon_slot[CHARA_XIAO] = 0;
    }

    std::fprintf(stderr, "coop: Xiao joins the party for player 2 (party %d -> 2; her weapon slot %d, item %d)\n", was,
                status->equipped_weapon_slot[CHARA_XIAO], status->chara_weapons[CHARA_XIAO][0].item_no);
}

// coop-p2doors test hook (DC_COOP_INDOOR_WALK_DOOR=K): once K of her steps have passed in the room (whatever Toan does
// there, his talks and events included), her stick points her at the room's first door (as she would push it, from her
// camera's angle) until she stands on it, as a walk there by hand: her own collision and facing, not a warp. Both
// linked copies step it alike. DC_COOP_DEBUG: her distance to the door every 30 steps.
static bool g_away_done = false; // coop-p2doors: DC_COOP_INDOOR_AWAY's warp done in this room (IndoorDoorForTest)

static void WalkToDoorForTest(float &lx, float &ly, float angle) {
    static const int at = TestEnvInt("DC_COOP_INDOOR_WALK_DOOR", 0);
    static char      room[16] = "";
    static int       steps = 0;
    sceVu0FVECTOR    door;
    float            rot = 0.0f;

    if (at <= 0 || !g_indoor.on) {
        return;
    }

    if (std::strncmp(room, EdInteriorName, sizeof room) != 0) {
        std::snprintf(room, sizeof room, "%s", EdInteriorName);
        steps = 0;
    }

    // (after DC_COOP_INDOOR_AWAY's warp, when a test sets one: she walks to the door once Toan is away from it)
    if (++steps < at || (TestEnv("DC_COOP_INDOOR_AWAY") != nullptr && !g_away_done) || !EdInDoorPose(door, &rot)) {
        return;
    }

    const float dx = door[0] - g_town.pos[0];
    const float dz = door[2] - g_town.pos[2];
    const float flat = std::hypot(dx, dz);

    if (CoopDebug() && steps % 30 == 0) {
        std::printf("coop: test: walk to the door at step %d (room mode %d): she at (%.1f %.1f %.1f), door (%.1f %.1f %.1f) facing %.2f, %.1f flat, %.1f in all\n", steps, g_indoor.mode,
                    g_town.pos[0], g_town.pos[1], g_town.pos[2], door[0], door[1], door[2], rot, flat,
                    std::sqrt(flat * flat + (door[1] - g_town.pos[1]) * (door[1] - g_town.pos[1])));
        std::fflush(stdout);
    }

    if (flat < 2.0f) {
        lx = ly = 0.0f;
        return;
    }

    // The walk's move_x = lx cos + ly sin, move_z = ly cos - lx sin, turned back.
    const float mx = dx / flat;
    const float mz = dz / flat;

    lx = mx * cosf(angle) - mz * sinf(angle);
    ly = mx * sinf(angle) + mz * cosf(angle);
}

// coop-p2ground test hook (DC_COOP_INDOOR_WALK_TO=K:X:Z[:X2:Z2]): once K of her steps have passed since she was placed in
// the room (the merge's place, too), her stick points her at (X, Z), then at (X2, Z2), then back, round and round (the
// next leg after 3 s when a wall holds her short of one), as a walk by hand (her own collision). Every 30 steps: where she is and the lowest she has been since the walk began.
static void WalkToSpotForTest(float &lx, float &ly, float angle) {
    static const char *spec = TestEnv("DC_COOP_INDOOR_WALK_TO");
    static int         leg = 0;
    static int         leg_steps = 0;
    static float       lowest = 0.0f;
    int                at = 0;
    float              to[2][2] = {};

    if (spec == nullptr || !g_indoor.on) {
        return;
    }

    const int n = std::sscanf(spec, "%d:%f:%f:%f:%f", &at, &to[0][0], &to[0][1], &to[1][0], &to[1][1]);

    if (n < 3 || g_indoor.ticks < at) {
        return;
    }

    if (n < 5) {
        to[1][0] = to[0][0];
        to[1][1] = to[0][1];
    }

    const int steps = g_indoor.ticks - at;

    if (steps == 0) {
        leg = 0;
        leg_steps = 0;
        lowest = g_town.pos[1];
    }

    lowest = std::min(lowest, g_town.pos[1]);

    const float dx = to[leg][0] - g_town.pos[0];
    const float dz = to[leg][1] - g_town.pos[2];
    const float flat = std::hypot(dx, dz);

    if (steps % 30 == 0) {
        std::fprintf(stderr, "coop: test: walk to a spot at step %d: she at (%.1f %.1f %.1f), lowest %.1f, leg %d (%.0f %.0f) %.1f flat\n", steps, g_town.pos[0],
                    g_town.pos[1], g_town.pos[2], lowest, leg, to[leg][0], to[leg][1], flat);
    }

    if (flat < 3.0f || ++leg_steps > 180) {
        leg ^= 1;
        leg_steps = 0;
        lx = ly = 0.0f;
        return;
    }

    const float mx = dx / flat;
    const float mz = dz / flat;

    lx = mx * cosf(angle) - mz * sinf(angle);
    ly = mx * sinf(angle) + mz * cosf(angle);
}

// coop-p2collide test hook (DC_COOP_INDOOR_WALK_INTO=K:villager|toan): once K of her steps out of the room's events have
// passed since she was placed in it, her stick points her at the nearest villager in play (or at Toan) for 360 steps, as a
// walk into them by hand. Every 30 steps (stderr): her flat distance to it, the nearest she has come, and its body's width
// (the radius its wall stands at, CCharacter::PickUpPoly).
static void WalkIntoForTest(float &lx, float &ly, float angle) {
    static const char *spec = TestEnv("DC_COOP_INDOOR_WALK_INTO");
    static float       nearest = 0.0f;
    int                at = 0;
    char               who[16] = "";

    if (spec == nullptr || !g_indoor.on || std::sscanf(spec, "%d:%15s", &at, who) != 2 || g_indoor.ticks < at || g_indoor.ticks >= at + 360) {
        return;
    }

    CCharacter *target = nullptr;
    int         index = -1;
    float       flat = 0.0f;

    if (std::strcmp(who, "toan") == 0) {
        target = Chara;
    } else {
        for (int i = 0; i < 10; i++) {
            sceVu0FVECTOR at_v;

            if (EdVillager[i].initialized == 0 || EdVillager[i].draw_enabled == 0) {
                continue;
            }

            EdVillager[i].GetPosition(at_v);
            const float d = std::hypot(at_v[0] - g_town.pos[0], at_v[2] - g_town.pos[2]);

            if (target == nullptr || d < flat) {
                target = &EdVillager[i];
                index = i;
                flat = d;
            }
        }
    }

    if (target == nullptr) {
        return;
    }

    sceVu0FVECTOR to;

    target->GetPosition(to);

    const float dx = to[0] - g_town.pos[0];
    const float dz = to[2] - g_town.pos[2];
    const int   steps = g_indoor.ticks - at;

    flat = std::hypot(dx, dz);
    nearest = steps == 0 ? flat : std::min(nearest, flat);

    if (steps % 30 == 0) {
        std::fprintf(stderr, "coop: test: walk into %s %d at step %d: she at (%.1f %.1f %.1f), it at (%.1f %.1f %.1f), %.1f flat, nearest %.1f, its width %.1f\n", who,
                     index, steps, g_town.pos[0], g_town.pos[1], g_town.pos[2], to[0], to[1], to[2], flat, nearest, target->body_width);
    }

    if (flat < 0.5f) {
        lx = ly = 0.0f;
        return;
    }

    const float mx = dx / flat;
    const float mz = dz / flat;

    lx = mx * cosf(angle) - mz * sinf(angle);
    ly = mx * sinf(angle) + mz * cosf(angle);
}

// coop-localfix: player 2's copy waiting to join (its game started, no link yet, not apart): she is drawn as herself there too.
// coop-localfix2: his Toan's arrival in the town her copy waits to join in (PartnerStep's prejoin placing).
struct PreJoinToan {
    bool  set = false;
    int   map = -1;
    float pos[3] = {};
    float yaw = 0.0f;
};
PreJoinToan g_prejoin_toan;

static bool PreJoinP2() {
    return NetplayRole() == 2 && !NetplayLinkUp() && !CoopWorldsApart() && !NetplayHasPlayer() && !Mutated("prejoin_toan");
}

// coop-doorfix (Jep's 2026-10-30 Local playtest at Norune's cave door): what the door's event 130 does to its user, played
// beside the game on that player's body, both copies on the same tick (the town's lockstep), so neither player stops the
// other. Read from event.stb (gedit/e01, program 130 at code byte 71604):
// - A wrong item from the Use Item window (_GOTO_USE_ITEM(236)'s answer not 236): CALL 7864 -> 6248 with motion 7: the
//   user turns to the camera over 100 ticks (_TURN_CHARA at 0.2) in motion 7 (flags 2), sound 21 at the 60th, then motion 0
//   and back to the old facing over 60 ticks (0.12). The window itself closed with its confirm sound (menu_save.cpp
//   EventItemSelectKey: a "Miss" pick is confirmed, nothing taken). No message.
// - The cave key (or a Cross at the opened door): the user put at (903.41 0 -497.29) facing 1.5, sound 323, the door's
//   "door1"/"door2" frames of fixed part 3 slide apart 0.3 a tick for 60 ticks, then the user walks (motion 2 at 0.4) to
//   (962 0 -499.24) at 0.6 a tick, turning at 0.1, while the screen fades out over 100 ticks; then _MAP_JUMP(201).
// The user's pad is held throughout (retail's event holds its player). Before, a wrong item was the picker's own "Nothing
// happens." note and the refuse sound, and the key split the pair on its tick, so the other screen lost the user at once.
// DC_COOP_MUTATE=cavenote (twin): the wrong item's note, as before. DC_COOP_MUTATE=cave_split_at_key (twin): the key splits
// the pair at once and the user's copy runs the event alone, as before. DC_COOP_MUTATE=cavedoor_off: both (the class check's
// fix off).
namespace {

constexpr int   kCaveRefusal = 1;
constexpr int   kCaveEntry = 2;
constexpr int   kRefusalTurn = 100; // ticks turning to the camera
constexpr int   kRefusalSound = 60; // its sound 21 on the 60th
constexpr int   kRefusalBack = 60;  // ticks turning back
constexpr int   kEntryDoor = 60;    // ticks the door slides
constexpr int   kEntryWalk = 100;   // ticks walking in under the fade
constexpr float kEntryStart[3] = {903.41f, 0.0f, -497.29f};
constexpr float kEntryTo[3] = {962.0f, 0.0f, -499.24f};
constexpr float kEntryYaw = 1.5f;

} // namespace

struct CaveAct {
    int   kind = 0;  // kCaveRefusal, kCaveEntry, 0 none
    int   ticks = 0; // its steps so far
    float yaw = 0.0f; // the user's facing at its start (refusal: turned back to)
    float from[4] = {}; // where the user stood at its start
    float look[4] = {}; // refusal: the user's camera's eye at its start
    bool  done = false; // entry: walked in, the split due (CoopTownCaveKeyUsed gives the user once)
    int   motion = 0;   // the motion it plays (put back when the game's walk or an event's end set another)
    int   flags = 0;
    float speed = -1.0f;
};

CaveAct g_cave_act[2]; // by player number - 1 (in snapshot-allow.txt: an act is game state on both copies)
int     g_cave_door_open = 0; // the door's slide (in steps of 0.3): opening during an entry, closing on the stayer's copy after it

bool CaveDoorFixOff(const char *twin) {
    return Mutated(twin) || Mutated("cavedoor_off");
}

// The player whose body is the game's own (Chara) on this copy: player 1 together; apart, this copy's player.
static int TownResident() {
    return CoopWorldsApart() ? NetplayRole() : 1;
}

// The body the player's act moves: the game's walker, or (together) the town partner's.
static CCharacter *CaveActor(int player) {
    if (player == TownResident()) {
        return Chara;
    }

    return CoopWorldsApart() ? nullptr : g_town.body;
}

static void CaveActorPos(int player, float *out) {
    if (player == TownResident()) {
        Chara->GetPosition(out);
    } else {
        sceVu0CopyVector(out, g_town.pos);
    }
}

static void CaveActorPlace(int player, const float *at) {
    if (player == TownResident()) {
        Chara->SetPosition(const_cast<float *>(at));
        return;
    }

    for (int i = 0; i < 3; i++) {
        g_town.pos[i] = at[i];
        g_town.velocity[i] = 0.0f;
    }
}

static float CaveActorYaw(int player) {
    sceVu0FVECTOR rotation;

    if (player == TownResident()) {
        Chara->GetRotation(rotation);
    } else {
        g_town.body->frame->GetRotation(rotation);
    }
    return rotation[1];
}

static void CaveActorFace(int player, float yaw) {
    if (player == TownResident()) {
        Chara->SetRotation(0.0f, yaw, 0.0f);
        return;
    }

    g_town.body->frame->SetRotation(0.0f, yaw, 0.0f);
    g_town.body->SetRotation(0.0f, yaw, 0.0f);
}

static void CaveActorMotion(int player, int motion, int flags, float speed) {
    CCharacter *body = CaveActor(player);

    if (player == TownResident()) {
        Chara->SetMotion(motion, flags);
        Chara->SetMotionSpeed(speed);
        return;
    }

    body->motion_no = motion;
    body->motion_flags = flags;
    body->motion_speed = speed;
    g_town.motion = motion;
}

// The user's camera's eye: the game's for the resident, hers for the partner together (both stepped on both copies).
static void CaveActorCameraEye(int player, float *out) {
    if (player != TownResident() && g_town_cam.ready && g_town_cam.camera != nullptr) {
        g_town_cam.camera->GetPos(out);
    } else {
        MainCamera.GetPos(out);
    }
}

static void CaveActorTurnTo(int player, const float *at, const float *to, float rate) {
    CaveActorFace(player, AngleInterpolate(CaveActorYaw(player), atan2f(to[0] - at[0], to[2] - at[2]), rate, INTERPOLATE_STEP));
}

// The act's motion, set now and kept (an event's end, EdEventFinish, sets the main character's to 0).
static void CaveActMotion(int player, CaveAct &act, int motion, int flags, float speed) {
    act.motion = motion;
    act.flags = flags;
    act.speed = speed;
    CaveActorMotion(player, motion, flags, speed);
}

// The door's two leaves, as _HOBJ_FIXPARTS(n, 3, "door1"/"door2") and _SET_OBJ_POS put them: each of fixed part 3's frames'
// named child at (0 0 -/+ 0.3 n).
static void CaveDoorSlide(int steps) {
    if (pEditGround == nullptr) {
        return;
    }

    CMapParts &part = pEditGround->fixed_parts[3];
    CFrame    *frames[9] = {part.frame[0], part.frame[1], part.frame[2], part.frame[3], part.GetCollisionFrame(), part.shadow_frame, part.shade_frame,
                            part.camera_frame, part.ripple_frame};

    for (int leaf = 0; leaf < 2; leaf++) {
        sceVu0FVECTOR at = {0.0f, 0.0f, (leaf == 0 ? -0.3f : 0.3f) * (float) steps, 1.0f};

        for (CFrame *frame : frames) {
            if (CFrame *named = frame != nullptr ? frame->SearchFrame((char *) (leaf == 0 ? "door1" : "door2")) : nullptr) {
                named->SetPosition(at);
            }
        }
    }
    g_cave_door_open = steps;
}

static void CaveActTrace(int player, const CaveAct &act, const char *what) {
    sceVu0FVECTOR at;

    CaveActorPos(player, at);
    std::fprintf(stderr, "coop-town: cave act: player %d %s step %d at (%.1f %.1f) yaw %.2f motion %d door %d %s at link tick %lld\n", player,
                 act.kind == kCaveRefusal ? "refusal" : "entry", act.ticks, at[0], at[2], CaveActorYaw(player), (int) CaveActor(player)->motion_no,
                 g_cave_door_open, what, (long long) NetplayLinkTick());
    std::fflush(stderr);
}

bool CoopTownCaveActs(int player) {
    return player >= 1 && player <= 2 && g_cave_act[player - 1].kind != 0;
}

// The pad the game reads (port 0) is held while its player acts, or (coop-cavehold) a door's message waits for their Cross.
bool CoopTownCaveHoldsGamePad() {
    return mode == GAME_MODE_EDIT && (CoopTownCaveActs(TownResident()) || coop::TownNoteHolds(TownResident()));
}

void CoopTownCaveRefusal(int player) {
    if (CaveActor(player) == nullptr || CoopTownCaveActs(player)) {
        return;
    }

    CaveAct &act = g_cave_act[player - 1];

    act = CaveAct{};
    act.kind = kCaveRefusal;
    act.yaw = CaveActorYaw(player);
    CaveActorPos(player, act.from);
    CaveActorCameraEye(player, act.look);
    CaveActMotion(player, act, 7, 2, -1.0f);
    if (player == TownResident()) {
        chara_mode |= 2; // EdMoveChara keeps off its motion, as off a landing's (ebattle.cpp), from this tick on
    }
    std::fprintf(stderr, "coop-town: cave act: player %d's wrong item at the cave door: retail's refusal on the user (motion 7, to the camera at (%.0f %.0f))\n",
                 player, act.look[0], act.look[2]);
    std::fflush(stderr);
}

// Whether the user's key (or Cross at the opened door) walks them in beside the game first (together), rather than the
// door's event on the user's copy at once (apart, or the twin).
bool CoopTownCaveEntry(int player) {
    if (CaveDoorFixOff("cave_split_at_key") || !CoopWorldsLinkedTogether() || CaveActor(player) == nullptr || CoopTownCaveActs(player)) {
        return false;
    }

    CaveAct &act = g_cave_act[player - 1];

    act = CaveAct{};
    act.kind = kCaveEntry;
    for (int i = 0; i < 3; i++) {
        act.from[i] = kEntryStart[i];
    }
    act.from[3] = 1.0f;
    CaveActorPlace(player, act.from);
    CaveActorFace(player, kEntryYaw);
    CaveActMotion(player, act, 0, 0, -1.0f);
    if (player == TownResident()) {
        chara_mode |= 2;
    }
    {
        SoundOwner sound(0); // the door is the world's: both screens hear it open
        SndSePlay(323, -1, 0);
    }
    CaveActTrace(player, act, "starts");
    return true;
}

// Each town tick (walk), both copies: the acts go on a step.
static void CaveActsStep() {
    // The stayer's door closes again after the other walked in (this copy alone, apart: the user's left the town).
    if (g_cave_door_open > 0 && !CoopTownCaveActs(1) && !CoopTownCaveActs(2)) {
        CaveDoorSlide(g_cave_door_open - 1);
        if (g_cave_door_open == 0) {
            std::fprintf(stderr, "coop-town: cave act: the cave door shut again on this copy at link tick %lld\n", (long long) NetplayLinkTick());
            std::fflush(stderr);
        }
    }

    for (int player = 1; player <= 2; player++) {
        CaveAct &act = g_cave_act[player - 1];

        if (act.kind == 0 || act.done) {
            continue;
        }
        if (CaveActor(player) == nullptr) { // a split or a merge since it began (the copies start again from one state)
            act = CaveAct{};
            chara_mode &= ~2;
            continue;
        }

        act.ticks++;

        // The user stands where the act has them (the game's walk would push the walker back out of the door's leaves) in
        // its motion.
        CaveActorPlace(player, act.from);
        if (CaveActor(player)->motion_no != act.motion) {
            CaveActorMotion(player, act.motion, act.flags, act.speed);
        }

        if (act.kind == kCaveRefusal) {
            if (player == TownResident()) {
                chara_mode |= 2; // EdMoveChara keeps off its motion, as off a landing's (ebattle.cpp)
            }

            if (act.ticks <= kRefusalTurn) {
                CaveActorTurnTo(player, act.from, act.look, 0.2f);
                if (act.ticks == kRefusalSound) {
                    SoundOwner sound(player); // the user's screen
                    SndSePlay(21, -1, 0);
                    std::fprintf(stderr, "coop-town: cave act: player %d's refusal sound 21 at step %d\n", player, act.ticks);
                }
                if (act.ticks == kRefusalTurn) {
                    CaveActMotion(player, act, 0, 0, -1.0f);
                    act.look[0] = act.from[0] + sinf(act.yaw);
                    act.look[2] = act.from[2] + cosf(act.yaw);
                }
            } else {
                CaveActorTurnTo(player, act.from, act.look, 0.12f);
            }

            if (act.ticks % 10 == 0 || act.ticks == 1) {
                CaveActTrace(player, act, "");
            }
            if (act.ticks >= kRefusalTurn + kRefusalBack) {
                CaveActTrace(player, act, "ends");
                act = CaveAct{};
                if (player == TownResident()) {
                    chara_mode &= ~2;
                }
            }
            continue;
        }

        // The entry: the door slides, then the user walks in under the fade.
        if (player == TownResident()) {
            chara_mode |= 2;
        }

        if (act.ticks <= kEntryDoor) {
            CaveDoorSlide(act.ticks);
            if (act.ticks == kEntryDoor) {
                CaveActMotion(player, act, 2, 0, 0.4f);
            }
        } else {
            CaveActorTurnTo(player, act.from, kEntryTo, 0.1f);
            const float yaw = CaveActorYaw(player);
            const float left = std::hypot(kEntryTo[0] - act.from[0], kEntryTo[2] - act.from[2]);
            const float step = std::min(0.6f, left);

            act.from[0] += step * sinf(yaw);
            act.from[2] += step * cosf(yaw);
            CaveActorPlace(player, act.from);
        }

        if (act.ticks % 10 == 0 || act.ticks == 1) {
            CaveActTrace(player, act, "");
        }
        if (act.ticks >= kEntryDoor + kEntryWalk) {
            act.done = true;
            CaveActTrace(player, act, "walked in: the split");
        }
    }
}

// The user's screen fades out as the user walks in (draw only, the user's copy).
void CoopTownCaveDraw() {
    const int local = NetplayLocalPlayer();

    if (local < 1 || local > 2 || g_cave_act[local - 1].kind != kCaveEntry) {
        return;
    }

    const int     ticks = g_cave_act[local - 1].ticks;
    const int     alpha = std::clamp((ticks - kEntryDoor) * 0x80 / kEntryWalk, 0, 0x80);
    const spRGBA  black = {0x00, 0x00, 0x00, (unsigned char) alpha};

    if (alpha > 0) {
        set2DSpriteC4(Vif1Packet, CRect_i_(0, 0, 640, 512), (spRGBA *) &black, (spRGBA *) &black, (spRGBA *) &black, (spRGBA *) &black);
    }
}

// The user's act body step in place of her walk (the partner's), as the act moved her.
static bool CaveActPartnerStep(int player) {
    if (!CoopTownCaveActs(player) || player == TownResident()) {
        return false;
    }

    CCharacter   *body = g_town.body;
    sceVu0FVECTOR rotation;

    body->motion_type.motion_info->speed = 0.05f;
    body->frame->SetPosition(g_town.pos);
    body->frame->GetRotation(rotation);
    body->SetPosition(g_town.pos);
    body->SetRotation(rotation[0], rotation[1], rotation[2]);
    body->Step();
    body->ShadowStep();
    body->ClothStep(0);
    g_town.visible = true;
    g_town.stood = true;
    return true;
}

// coop-doorfade: player 1's copy, his door opening apart: she stands where she was (her pad is no longer this copy's to
// know), at rest, drawn through the fade.
static bool g_doorfade_stood = false; // said once a door
// coop-dunentry: how long her way out's fade runs on her screen, as his copy keeps her drawn (ED_MODE_DOOR_OPEN's 101 frames)
constexpr long long kHerLeaveMs = 1700;

static void StandThroughDoorFade() {
    CCharacter *body = g_town.body;

    body->motion_type.motion_info->speed = 0.05f;
    if (const int motion = keyCtrl(0.0f, 0.0f, body->motion_type.motion_info); motion != g_town.motion) {
        body->motion_no = motion;
        body->motion_flags = 0;
        body->motion_speed = -1.0f;
        g_town.motion = motion;
    }

    body->Step();
    body->ShadowStep();
    body->ClothStep(0);
    g_town.visible = true;

    if (!g_doorfade_stood) {
        g_doorfade_stood = true;
        std::printf("coop: his door opens apart: player 2 stands at (%.0f %.0f %.0f) through its fade\n", g_town.pos[0], g_town.pos[1], g_town.pos[2]);
    }
}

// The partner's step, in town or (g_indoor.on) inside a building.
// coop-soakfix3: a join's stand taken inside a town event (kSpawnAfterEvent): on the first walk after the event's end, on every copy alike
// (the town's walk, both copies' one tick), she goes to spot 2 of Toan's stand there (his place the event's end gave him), and what her
// body stood on through the event (its gate's ground) starts nothing.
static void StandAfterEvent() {
    if (!g_stand_after_event || ::GameMode != ED_MODE_WALK || EdInteriorFlag != 0 || Chara == nullptr || g_town.body == nullptr ||
        g_town.body->frame == nullptr) {
        return;
    }
    g_stand_after_event = false;

    sceVu0FVECTOR toan;
    sceVu0FVECTOR rotation;
    float         spot[3];
    float         ground = 0.0f;
    int           kind = 0;

    Chara->GetPosition(toan);
    Chara->GetRotation(rotation);
    TownSecondSpot(toan, rotation[1], toan, spot);
    g_town.pos[0] = spot[0];
    g_town.pos[1] = spot[1];
    g_town.pos[2] = spot[2];
    g_town.pos[3] = 1.0f;
    g_town.velocity[0] = g_town.velocity[1] = g_town.velocity[2] = 0.0f;
    g_town.body->frame->SetPosition(g_town.pos);
    g_town.body->frame->SetRotation(0.0f, rotation[1], 0.0f);
    g_town.body->SetPosition(g_town.pos);
    g_town.body->SetRotation(0.0f, rotation[1], 0.0f);
    g_partner_ground_event = 0;
    TownGroundAt(spot[0], spot[1], spot[2], &ground, &kind);
    g_partner_ground_kind = kind;

    const float dx = spot[0] - toan[0];
    const float dz = spot[2] - toan[2];

    std::printf("coop: P2 placed at spot 2 of Toan's after the town event at (%.0f %.0f %.0f), Toan at (%.0f %.0f %.0f), %.0f apart, ground event %d, link tick %lld\n",
                spot[0], spot[1], spot[2], toan[0], toan[1], toan[2], std::sqrt(dx * dx + dz * dz), kind, NetplayLinkTick());
    std::fflush(stdout);
}

static void PartnerStep() {
    StandAfterEvent();
    PresenceTrace("town", g_town.visible); // coop-p2absent
    StandInWeaponStep(); // coop: p2weapon
    const bool was_visible = g_town.visible; // coop-doorfade
    g_town.visible = false;
    g_town.stood = false; // coop-nopush
    g_town_skin = false;

    if (!CoopWorldsApart()) {
        g_town_skin_said = false;
    }

    // coop-worlds: player 2's copy apart draws her in Toan's place. DC_COOP_MUTATE=toan_skin: Toan, as before (twin).
    // Only while the link is down: a catch-up join's replay and its first lockstep ticks (until the merge) are still
    // apart, but the host, apart as player 1, steps no skin there, and the skin's partner state (loaded, then put on
    // Toan's spot) would part the copies at the merge. DC_COOP_MUTATE=replay_skin (twin): the skin then too.
    // coop-localfix (Jep's 2026-10-26 retest: "on load in, P2 is still Toan until P1 finishes loading"; his rule: she is never
    // Toan): her copy waiting to join (player 2's, its game started, no link yet) draws her too. DC_COOP_MUTATE=prejoin_toan
    // (twin): Toan until the join, as before.
    const bool prejoin = PreJoinP2();

    // coop-localfix (Jep's 2026-10-26 retest: "P2 ... still refreshes their position once P1 finishes the town splash art"):
    // waiting to join, her copy has her where the host will put her as she joins (beside his Toan at the town's arrival, as
    // PlaceBesideToan does), not on Toan's own spot, so the join does not move her. DC_COOP_MUTATE=prejoin_on_toan (twin).
    static bool prejoin_placed = false;

    if (prejoin && !prejoin_placed && Chara != nullptr && Chara->frame != nullptr && !Mutated("prejoin_on_toan")) {
        sceVu0FVECTOR at;
        sceVu0FVECTOR rotation;

        prejoin_placed = true;
        Chara->GetPosition(at);
        Chara->GetRotation(rotation);
        // coop-localfix2: where his Toan arrives (the town's arrival, on the host's copy as on hers), for a join that goes by
        // her state (CoopTownBecomePartner puts Toan there).
        g_prejoin_toan.set = true;
        g_prejoin_toan.map = MapNo;
        g_prejoin_toan.yaw = rotation[1];
        for (int i = 0; i < 3; i++) {
            g_prejoin_toan.pos[i] = at[i];
        }
        at[0] -= kTownSpawn * cosf(rotation[1]);
        at[2] += kTownSpawn * sinf(rotation[1]);
        Chara->SetPosition(at);
        Chara->frame->SetPosition(at);
        std::printf("coop: before her join she stands beside Toan's arrival at (%.0f %.0f %.0f)\n", at[0], at[1], at[2]);
    }

    // (coop-stage4 4e part 2: her event carried through a merge: her skin stays the game's player on every copy till it ends)
    if (!g_indoor.on && Enabled() && (CoopTownSwapDeferred() || ((CoopWorldsApart() || prejoin) && NetplayRole() == 2 && (!NetplayLinkUp() || Mutated("replay_skin")) && !NetplayStayerMerge())) && Chara != nullptr &&
        Chara->frame != nullptr && pEditGround != nullptr && SaveData != nullptr && SaveData->GetDngStatus() != nullptr && !Mutated("toan_skin")) {
        g_indoor.was = false; // coop-inbuild: her skin stands in town
        TownSkinStep();
        return;
    }

    // coop-doorfade (Jep, 2026-10-27: "P2 fades from P1's screen immediately when P1 opens the door instead of staying while
    // it's fading out"): his Cross on a door splits the pair on its tick and his copy goes on apart, so the step below put her
    // away as his door began to open. Player 1's copy keeps her standing where she was, drawn, until his door's fade has
    // run (the room's load puts her away). DC_COOP_MUTATE=doorfade_hides (twin): put away at once, as before.
    // coop-pt1108b: DC_COOP_TEST_DUSK_APART=N: N town ticks after this copy (player 1), apart outdoors, hears the other copy in its zone, the clock to just before dusk, once: the time of
    // day's event runs as her merge starts. coop-pt1109e: counted from hearing her, not from the split: from the split it raced her two loads, and a merge after the event's end
    // tested nothing (the twin scene_arriver_own passed).
    if (static const int dusk_at = TestEnvInt("DC_COOP_TEST_DUSK_APART", 0); dusk_at > 0) {
        static int apart_ticks = 0;

        if (CoopWorldsApart() && CoopWorldsPeerHeardHere() && SaveData != nullptr && ++apart_ticks == dusk_at) {
            NowTime = 2.4999f;
            SaveData->SetNowTime(NowTime);
            std::fprintf(stderr, "coop: test: dusk comes %d ticks after she is heard here, his time apart\n", dusk_at);
            std::fflush(stderr);
        }
    }

    // coop-pt1109c: DC_COOP_TEST_BGM_APART=N: 60 town ticks after this copy went apart, a scene's music, track N, starts on it: as a scene's start, run before the join.
    if (static const int bgm_track = TestEnvInt("DC_COOP_TEST_BGM_APART", -1); bgm_track >= 0) {
        static int bgm_ticks = 0;

        if (CoopWorldsApart() && ++bgm_ticks == 60) {
            SndBgmLoad(bgm_track);
            SndBgmPlay(0);
            std::fprintf(stderr, "coop: test: track %d started on this copy apart\n", bgm_track);
            std::fflush(stderr);
        }
    }

    if (was_visible && CoopWorldsApart() && !NetplayStayerMerge() && NetplayRole() != 2 && ::GameMode == ED_MODE_DOOR_OPEN && EdInteriorFlag == 0 &&
        g_town.body != nullptr && g_town.body->frame != nullptr && !Mutated("doorfade_hides")) {
        StandThroughDoorFade();
        return;
    }

    // coop-dunentry (Jep's 2026-10-30 playtest, item 14: "P2 vanishes on P1's screen the moment she picks go into the dungeon"):
    // her own way out (her town door, or the event that walks her into a dungeon, which splits the pair as it starts) left
    // his copy apart on its tick, and the step below put her away at once, though her screen still shows her walk and its
    // fade. Player 1's copy keeps her standing where she was, drawn, for as long as that fade runs on hers (kHerLeaveMs, the
    // door's ED_MODE_DOOR_OPEN length, inference for the dungeon's walk-in), as coop-doorfade does through his own door.
    // Draw-only, his copy apart. DC_COOP_MUTATE=herleave_hides (twin): put away at once, as before.
    static int leave_drawn = 0;

    if (const long long left_ms = CoopWorldsHerLeaveMs(); left_ms >= 0 && !Mutated("herleave_hides")) {
        if (left_ms < kHerLeaveMs && (was_visible || leave_drawn > 0) && !NetplayStayerMerge() && EdInteriorFlag == 0 && g_town.body != nullptr &&
            g_town.body->frame != nullptr) {
            if (leave_drawn++ == 0) {
                std::printf("coop: her way out apart: player 2 stands at (%.0f %.0f %.0f) through her fade\n", g_town.pos[0], g_town.pos[1], g_town.pos[2]);
            }
            StandThroughDoorFade();
            // coop-pilotfix: DC_COOP_TEST_DUSK_AT_LEAVE=N: N town ticks into her way out on this copy, still drawing her through
            // her fade, the clock to just before dusk, once: his dusk starts with her shown here and holds that through the event
            // (the town walk, and with it this step, does not run in an event), hers has her hidden: pt1108b's play. Counted from
            // hearing her come back (DC_COOP_TEST_DUSK_APART) the fade is over by then whenever the copies run at play speed
            // (kHerLeaveMs is real time), both had her hidden, and scene_arriver_own's twin had nothing to fail on.
            if (static const int dusk_leave = TestEnvInt("DC_COOP_TEST_DUSK_AT_LEAVE", 0); dusk_leave > 0 && leave_drawn == dusk_leave && SaveData != nullptr) {
                NowTime = 2.4999f;
                SaveData->SetNowTime(NowTime);
                std::fprintf(stderr, "coop: test: dusk comes %d ticks into her way out, player 2 drawn here (shown %d)\n", dusk_leave, g_town.visible ? 1 : 0);
                std::fflush(stderr);
            }
            return;
        }
        if (leave_drawn > 0) {
            std::fprintf(stderr, "coop: her way out apart: player 2 drawn %d frames on player 1's copy after she left\n", leave_drawn);
            leave_drawn = -1; // said; until her next way out
        }
    } else if (left_ms < 0) {
        leave_drawn = 0;
    }

    if (!Enabled() || Chara == nullptr || Chara->frame == nullptr || pEditGround == nullptr || SaveData == nullptr || SaveData->GetDngStatus() == nullptr ||
        (CoopWorldsApart() && !NetplayStayerMerge())) { // coop-worlds: apart, no partner (coop-localfix2: but her own merge's)
        static bool said = false;

        if (!said && CoopDebug()) {
            said = true;
            std::printf("coop-debug: town partner off: coop %d chara %d frame %d ground %d status %d\n", (int) Enabled(), Chara != nullptr,
                        Chara != nullptr && Chara->frame != nullptr, pEditGround != nullptr, SaveData != nullptr);
        }

        return;
    }

    // No partner without a player 2 (solo, or an online host still waiting for its guest); a pad or link
    // that drops out for a moment changes nothing, and one who comes back finds the partner where it was
    // left (StepPresence). Only the twin re-places it.
    bool rejoined = false;

    TownWarpForTest(); // test hooks (the clock one runs without a player 2 too)

    if (!StepPresence(&rejoined)) {
        return;
    }

    if (rejoined && g_town.loaded >= 0 && g_town.body != nullptr) {
        PlaceBesideToan("rejoin (twin)");
    }

    int chara = TownMember();

    if (g_town.loaded != chara || g_town.map != MapNo || BlockEmptyNow()) {
        if (!TownLoad(chara)) {
            g_town.loaded = -1;
            return;
        }
    }

    // coop-inbuild: in or out of a building since her last step: she starts by Toan on this side of the door (indoors a
    // step into the room before him), and her camera afresh from the room's or the town's.
    if (g_indoor.on != g_indoor.was) {
        g_indoor.was = g_indoor.on;
        g_town.airborne = 0;
        g_town.has_ground = false;

        if (g_indoor.on) {
            PlaceInRoom();
        } else {
            PlaceBesideToan("out of a building");
        }

        g_town_cam.ready = false;
    }

    InputPadState pad = Pad2();

    // coop-gatepopup test hook (DC_WORLDS_TEST_QUESTION_STICK=1, both copies): her stick held right while a door's call is open.
    static const bool question_stick = TestEnvOn("DC_WORLDS_TEST_QUESTION_STICK");

    if (question_stick && (CoopWorldsDoorAsks(1) || CoopWorldsDoorAsks(2))) {
        pad.left_x = 0xFF;
    }

    // coop-town: player 2 at her own Georama stands (her pad is the editor's); coop-townmenus: and in her own menu, her
    // pause (Jep, 2026-10-24: her stick walked her under it). DC_COOP_MUTATE=townmenu_walks: she walks on (twin).
    if (CoopP2Building() || ((coop::PlayerInMenu(2, coop::kActorMenuTown) || CoopP2TownPaused()) && !Mutated("townmenu_walks")) ||
        coop::TownDoorPickOpenFor(2) || // coop-doorprompt: and at her picker by the cave door
        CoopTownCaveActs(2) ||          // coop-doorfix: and while the cave door acts on her
        coop::TownNoteHolds(2) ||       // coop-cavehold: and while a door's message waits for her Cross
        CoopWorldsQuestionHolds(1)) {   // coop-gatepopup: and in a gate's question, asking or asked
        pad = InputPadState{};
        pad.connected = true;
    }

    StepTownWheel(pad, g_town.loaded); // pad 2's character wheel (R3), as in the dungeons

    // Linked: player 2's own camera, and pad 2 walks relative to it. Couch: Toan's, the one on screen.
    if (Linked() && g_indoor.on) {
        if (!g_town_cam.ready) {
            IndoorCamInit();
        }

        IndoorCamStep(pad); // coop-inbuild
    } else if (Linked()) {
        TownCamStep(pad);
    }

    // coop-doorfix: the cave door's act moves her (her refusal, her walk in); she is stepped and shown where it has her.
    if (CaveActPartnerStep(2)) {
        return;
    }
    float         lx = Stick(pad.left_x);
    float         ly = Stick(pad.left_y);

    if (Mutated("townwalk")) {
        lx = ly = 0.0f;
    }

    g_indoor.stick[0] = lx; // coop-inbuild: the debug line's
    g_indoor.stick[1] = ly;

    float angle = Linked() && g_town_cam.ready ? g_town_cam.camera->GetAngleH()
                  : EdMoveCharaInfo.camera != nullptr ? EdMoveCharaInfo.camera->GetAngleH()
                                                      : MainCamera.GetAngleH();

    WalkToDoorForTest(lx, ly, angle); // coop-p2doors
    WalkToSpotForTest(lx, ly, angle); // coop-p2ground
    WalkIntoForTest(lx, ly, angle);   // coop-p2collide
    float move_x = lx * cosf(angle) + ly * sinf(angle);
    float move_z = ly * cosf(angle) - lx * sinf(angle);

    // coop-localfix2 test trace (DC_COOP_TEST_HER_TRACE=1): her step's inputs and place each tick, to diff the two copies.
    if (static const bool trace = TestEnv("DC_COOP_TEST_HER_TRACE") != nullptr; trace) {
        float facing[4] = {};

        g_town.body->frame->GetRotation(facing);
        std::fprintf(stderr, "her-trace: tick %lld pos %.3f %.3f %.3f stick %.3f %.3f angle %.5f cam %d linked %d air %d facing %.4f\n",
                     (long long) NetplayLinkTick(), g_town.pos[0], g_town.pos[1], g_town.pos[2], lx, ly, angle, (int) g_town_cam.ready, (int) Linked(),
                     g_town.airborne, facing[1]);
    }
    float speed = g_indoor.on || EdMoveCharaInfo.interior != 0 ? 1.0f : 1.6f;

    g_town.velocity[0] = move_x * speed;
    g_town.velocity[2] = move_z * speed;

    CCharacter *body = g_town.body;

    if (move_x != 0.0f || move_z != 0.0f) {
        body->frame->SetRotation(0.0f, atan2f(move_x, move_z), 0.0f);
    }

    CCPoly       *polys;
    int           count = GatherTownPolys(g_town.pos, &polys);
    sceVu0FVECTOR moved;
    MoveCheckInfo info;

    count += PartnerBodyPolys(g_town.pos, g_town.velocity[0] != 0.0f || g_town.velocity[2] != 0.0f, &polys[count]); // coop-p2collide, coop-nopush

    MoveCheck(g_town.pos, g_town.velocity, moved, &info, polys, count, 0);
    g_town.velocity[1] -= 0.1f;

    if (g_town.velocity[1] < -10.0f) {
        g_town.velocity[1] = -10.0f;
    }

    sceVu0CopyVector(g_town.pos, moved);

    // coop-p2ground (Jep's 2026-10-27 run: "P2 becomes able to fall out of the map when P2 is in the mayor's house with P1"):
    // a room's parts leave holes in its floor (the mayor's, round its lower half), and EdMoveChara stands Toan on the
    // room's floor at 0 below them; her walk stands on it too. DC_COOP_MUTATE=indoor_no_floor (twin): she falls through.
    const bool floor = (g_indoor.on || EdMoveCharaInfo.interior != 0) && g_town.pos[1] < 0.0f && !Mutated("indoor_no_floor");

    if (floor) {
        g_town.pos[1] = 0.0f;
    }

    if (info.landed != 0 || floor) {
        g_town.velocity[1] = 0.0f;
        g_town.airborne = 0;
        sceVu0CopyVector(g_town.ground, g_town.pos);
        g_town.has_ground = true;
        g_town.high_camera = info.landed != 0 && info.ground_poly.attr.area_kind == AREA_KIND_HIGH_CAMERA;
        if (info.landed != 0) {
            body->SetFootSoundID(info.ground_poly.attr.foot_sound); // coop-doorfade: her footsteps, by the ground
        }

        // coop-worlds: the event of the polygon she stepped onto (once per arrival on it), for her town exits. (The
        // room's floor at 0 is no polygon: kind 0.)
        const int kind = info.landed != 0 ? info.ground_poly.attr.ground_kind : 0;

        if (!g_indoor.on && kind > 0 && kind != g_partner_ground_kind) { // coop-inbuild: a room's are Toan's
            g_partner_ground_event = kind;
        }

        g_partner_ground_kind = kind;
    } else {
        g_town.airborne++;
    }

    // The partner roams the town freely: exits, doors and talk key off Toan (Chara), so walking onto
    // them does nothing for it. Only when it is off the walkable ground (fallen through, or two seconds
    // without a floor under it) is it rescued: back on the last ground it stood on, or beside Toan if
    // it never stood on any. DC_COOP_MUTATE=townleash brings back the old 200-unit leash (twin).
    if (g_town.airborne > kTownAirborneTicks || (g_town.has_ground && g_town.pos[1] < g_town.ground[1] - kTownFall)) {
        if (g_town.has_ground) {
            sceVu0CopyVector(g_town.pos, g_town.ground);
            g_town.velocity[0] = g_town.velocity[1] = g_town.velocity[2] = 0.0f;
        } else {
            PlaceBesideToan("rescue: off the ground, never stood on any");
        }

        std::fprintf(stderr, "coop: town partner rescued to (%.0f %.0f %.0f) (%s)\n", g_town.pos[0], g_town.pos[1], g_town.pos[2],
                    g_town.airborne > kTownAirborneTicks ? "airborne too long" : "fell below its last ground");
        g_town.airborne = 0;
    }

    if (Mutated("townleash")) {
        sceVu0FVECTOR toan;

        Chara->GetPosition(toan);

        float dx = g_town.pos[0] - toan[0];
        float dz = g_town.pos[2] - toan[2];

        if (dx * dx + dz * dz > 200.0f * 200.0f) {
            PlaceBesideToan("townleash (twin)");
        }
    }

    // The motion as the dungeon picks it for the stick (keyCtrl: standing, walking, running), playing at
    // the pace the dungeon gives it; set to 0, the motion would hold its first, bind pose.
    body->motion_type.motion_info->speed = Mutated("townidle") ? 0.0f : 0.05f;
    int motion = keyCtrl(lx, ly, body->motion_type.motion_info);

    if (motion != g_town.motion) {
        body->motion_no = motion;
        body->motion_flags = 0;
        body->motion_speed = -1.0f;
        g_town.motion = motion;
    }

    sceVu0FVECTOR rotation;

    body->frame->SetPosition(g_town.pos);
    body->frame->GetRotation(rotation);
    body->SetPosition(g_town.pos);
    body->SetRotation(rotation[0], rotation[1], rotation[2]);
    // Osmond's propeller turns in town too (coop_combat.cpp SpinPropeller).
    if (g_town.loaded == CHARA_OSMOND) {
        SpinPropeller(body, &g_town.propeller);
    }

    body->FootSoundEnable(Mutated("town_silent") ? 0 : 1); // coop-footsteps: the skin's are off (TownSkinStep)
    body->Step();
    FootstepNote(body, "her"); // coop-doorfade
    body->ShadowStep();
    body->ClothStep(0); // its cloth (Goro's cape) hangs from it, as in the dungeons
    g_town.visible = true;
    g_town.stood = true; // coop-nopush
    TownDebugLog();

    if (g_indoor.on) {
        if (g_indoor.mode != 4) { // the warps count ticks out of the room's events (an event moves Toan as it likes)
            g_indoor.ticks++;
        }

        IndoorDebugLog();
    }
}

// Test hooks (DC_COOP_INDOOR_DOOR=K[:toan], DC_COOP_INDOOR_AWAY=K[:toan]): once K of her steps out of the room's events
// have passed since she was placed in it, she (or Toan, on the first tick after that he walks: a talk turns him to the
// villager) is put at the room's first door, facing the way that takes it (DOOR), or 45 units from it into the room,
// beyond the reach of going out together (AWAY), as a walk there would leave them. Each once per room. Both linked copies
// read the same values on the same step. coop-fx51c (Jep, 2026-11-04: no fixed tick waits): K "loaded": on her first step
// once a state file (a row's fixture, taken in the room) is put back.
static void IndoorWarpForTest(const char *name, float away, bool &done) {
    const char *spec = TestEnv(name);
    int         at = 0;
    char        who[8] = "";
    const bool  on_load = spec != nullptr && std::strncmp(spec, "loaded", 6) == 0;

    if (g_indoor.ticks == 0) {
        done = false;
    }

    if (done || spec == nullptr || (on_load ? std::sscanf(spec + 6, ":%7s", who) < 0 || !NetplayStateFileLoaded() : std::sscanf(spec, "%d:%7s", &at, who) < 1 || g_indoor.ticks < at) ||
        (std::strcmp(who, "toan") == 0 && g_indoor.mode != 0)) {
        return;
    }

    done = true;

    sceVu0FVECTOR pos;
    float         rot = 0.0f;

    if (!EdInDoorPose(pos, &rot)) {
        std::printf("coop: test: indoor warp: the room %s has no door\n", EdInteriorName);
        return;
    }

    pos[0] -= away * sinf(rot);
    pos[2] -= away * cosf(rot);
    pos[3] = 1.0f;

    // coop-housefade test hook (DC_COOP_INDOOR_AWAY_AT=X:Z): AWAY puts her at (X, Z) on the door's floor instead (in his view
    // at his door, still beyond going out together).
    if (float x = 0.0f, z = 0.0f; away > 0.0f && std::strcmp(who, "toan") != 0 && std::sscanf(TestEnv("DC_COOP_INDOOR_AWAY_AT") != nullptr ? TestEnv("DC_COOP_INDOOR_AWAY_AT") : "", "%f:%f", &x, &z) == 2) {
        pos[0] = x;
        pos[2] = z;
    }

    if (std::strcmp(who, "toan") == 0) {
        Chara->SetPosition(pos);
        Chara->frame->SetPosition(pos);
        Chara->SetRotation(0.0f, rot, 0.0f);
        Chara->Step();
    } else {
        sceVu0CopyVector(g_town.pos, pos);
        g_town.velocity[0] = g_town.velocity[1] = g_town.velocity[2] = 0.0f;
        g_town.body->frame->SetRotation(0.0f, rot, 0.0f);
        g_town.body->SetRotation(0.0f, rot, 0.0f);
    }

    std::printf("coop: test: indoor warp (%s) at step %d: %s to (%.1f %.1f %.1f) facing %.2f\n", away > 0.0f ? "into the room" : "the door",
                g_indoor.ticks, who[0] != '\0' ? who : "player 2", pos[0], pos[1], pos[2], rot);
    std::fflush(stdout);

    // coop-doorflash: a frame series counted from the leaver's warp to the door (DC_COOP_TEST_SHOT_SERIES_MARK), so it takes in
    // his door's press, the split and the frames after it on her copy.
    if (away == 0.0f) {
        NetplayTestSeriesMark();
    }
}

static void IndoorDoorForTest() {
    static bool door_done = false;

    IndoorWarpForTest("DC_COOP_INDOOR_DOOR", 0.0f, door_done);
    // (coop-cand56: DC_COOP_INDOOR_AWAY_DIST=D puts it D from the door, for w-indoor-walkout: her Cross opens the door up to about 8 short
    // of its point and Toan settles about 3 nearer after the warp, so 45 left him 37.6 from her Cross, inside kDoorTogether's 40 under
    // the row's door_together set-up: both went out, passing or failing by where her press landed on her walk)
    IndoorWarpForTest("DC_COOP_INDOOR_AWAY", (float) TestEnvInt("DC_COOP_INDOOR_AWAY_DIST", 45), g_away_done);
}

// coop-p2doors test hook (DC_COOP_INDOOR_DOOR_APART=K): player 2's copy apart inside a building (she went in alone) puts
// its player, her, at the room's first door facing out once K of its walking steps have passed in the room, as her walk
// there would; once per room.
static void ApartDoorForTest(int mode) {
    static char room[16] = "";
    static int  steps = 0;
    static bool done = false;

    const int at = TestEnvInt("DC_COOP_INDOOR_DOOR_APART", 0);

    if (at <= 0 || NetplayRole() != 2 || Chara == nullptr || Chara->frame == nullptr) {
        return;
    }

    if (std::strncmp(room, EdInteriorName, sizeof room) != 0) {
        std::snprintf(room, sizeof room, "%s", EdInteriorName);
        steps = 0;
        done = false;
    }

    if (done || mode != 0 || ++steps < at) { // ED_IN_MODE_WALK
        return;
    }

    done = true;

    sceVu0FVECTOR pos;
    float         rot = 0.0f;

    if (!EdInDoorPose(pos, &rot)) {
        std::printf("coop: test: apart door warp: the room %s has no door\n", EdInteriorName);
        return;
    }

    pos[3] = 1.0f;
    Chara->SetPosition(pos);
    Chara->frame->SetPosition(pos);
    Chara->SetRotation(0.0f, rot, 0.0f);
    Chara->Step();
    std::printf("coop: test: apart door warp in %s at step %d: player 2 to (%.1f %.1f %.1f) facing %.2f\n", EdInteriorName, steps, pos[0], pos[1], pos[2], rot);
    std::fflush(stdout);
}

// coop-closet: where the game's own search (EdGetEvent) finds the room's event N from a pose near from: a scan of the floor
// around it, 2 units and eight facings apart. False when none is in reach.
static bool EventSpotNear(const float *from, int event, float *pos, float *yaw) {
    for (int r = 0; r <= 120; r += 2) {
        for (int dx = -r; dx <= r; dx += 2) {
            for (int dz = -r; dz <= r; dz += 2) {
                if (std::max(std::abs(dx), std::abs(dz)) != r) {
                    continue;
                }

                for (int f = 0; f < 8; f++) {
                    sceVu0FVECTOR  at = {from[0] + dx, from[1], from[2] + dz, 1.0f};
                    sceVu0FVECTOR  rot = {0.0f, -PI + f * (PI / 4.0f), 0.0f, 0.0f};
                    ED_EVENT_PARAM param;

                    if (EdGetEvent(EdInInfo->event_points, 32, &param, at, rot, NowTime) == 0 || param.kind != ED_EVENT_POINT_EVENT ||
                        param.point == nullptr || param.point->side != event) {
                        continue;
                    }

                    std::copy(at, at + 4, pos);
                    *yaw = rot[1];
                    return true;
                }
            }
        }
    }

    return false;
}

// coop-closet test hook (DC_COOP_INDOOR_EVENT_APART=K:N): player 2's copy apart inside a building puts its player, her,
// where the game's own search (EdGetEvent, from her pose) finds the room's event N (the mayor's closet, 12), once K of
// its walking steps have passed in the room, as her walk there would (EventSpotNear); once per room. Her Cross there then
// starts it as any of hers would.
static void ApartEventForTest(int mode) {
    static char room[16] = "";
    static int  steps = 0;
    static bool done = false;

    static const char *const spec = TestEnv("DC_COOP_INDOOR_EVENT_APART");
    int                      at = 0, event = -1;

    if (spec == nullptr || std::sscanf(spec, "%d:%d", &at, &event) != 2 || NetplayRole() != 2 || Chara == nullptr || Chara->frame == nullptr ||
        EdInInfo == nullptr) {
        return;
    }

    if (std::strncmp(room, EdInteriorName, sizeof room) != 0) {
        std::snprintf(room, sizeof room, "%s", EdInteriorName);
        steps = 0;
        done = false;
    }

    if (done || mode != 0 || ++steps < at) { // ED_IN_MODE_WALK
        return;
    }

    done = true;

    sceVu0FVECTOR from;
    sceVu0FVECTOR pos;
    float         yaw = 0.0f;

    Chara->GetPosition(from);

    if (!EventSpotNear(from, event, pos, &yaw)) {
        std::printf("coop: test: apart event warp: no event %d in reach in %s\n", event, EdInteriorName);
        std::fflush(stdout);
        return;
    }

    Chara->SetPosition(pos);
    Chara->frame->SetPosition(pos);
    Chara->SetRotation(0.0f, yaw, 0.0f);
    Chara->Step();
    std::printf("coop: test: apart event warp in %s at step %d: player 2 to (%.1f %.1f %.1f) facing %.2f, at event %d\n", EdInteriorName, steps, pos[0],
                pos[1], pos[2], yaw, event);
    std::fflush(stdout);
}

// coop-mayorhouse test hook (DC_COOP_TEST_ROOM_TALK=N:M): Toan put before the room's first villager (the mayor, i01h06),
// facing him, so his next Cross talks to him: on player 1's copy apart, at his Nth walk step in the room; once that talk is
// over, turned away (his later Crosses talk to nobody); and together, on both copies (the same link tick), on the first walk
// step M link ticks after the merge, for a talk she is in the room for.
static void RoomTalkForTest(int mode) {
    static const char *const spec = TestEnv("DC_COOP_TEST_ROOM_TALK");
    static int               at = -1, after = -1, steps = 0, phase = 0; // phase: 0 waits, 1 placed, 2 talking, 3 turned away, 4 placed together

    if (spec == nullptr || Chara == nullptr || Chara->frame == nullptr || EdInteriorFlag == 0) {
        return;
    }
    if (at < 0 && std::sscanf(spec, "%d:%d", &at, &after) < 1) {
        at = 1 << 30;
    }

    CNPCharacter &villager = EdVillager[0];

    if (villager.initialized == 0 || villager.draw_enabled == 0) {
        return;
    }

    auto place = [&](bool toward, const char *why) {
        sceVu0FVECTOR him;
        sceVu0FVECTOR toan;

        villager.GetPosition(him);
        Chara->GetPosition(toan);
        float dx = toan[0] - him[0], dz = toan[2] - him[2];
        const float len = std::sqrt(dx * dx + dz * dz);

        if (toward) {
            dx = len > 0.01f ? dx / len : 0.0f;
            dz = len > 0.01f ? dz / len : 1.0f;
            toan[0] = him[0] + dx * 9.0f;
            toan[2] = him[2] + dz * 9.0f;
        }
        const float yaw = std::atan2(him[0] - toan[0], him[2] - toan[2]) + (toward ? 0.0f : 3.14159265f);

        Chara->SetPosition(toan);
        Chara->frame->SetPosition(toan);
        Chara->SetRotation(0.0f, yaw, 0.0f);
        Chara->Step();
        std::fprintf(stderr, "coop: test: room talk: Toan %s the villager at (%.1f %.1f %.1f): at (%.1f %.1f %.1f) facing %.2f (%s, link tick %lld)\n",
                     toward ? "before" : "turned away from", him[0], him[1], him[2], toan[0], toan[1], toan[2], yaw, why, (long long) NetplayLinkTick());
        std::fflush(stderr);
    };

    if (CoopWorldsApart()) {
        // (coop-stage4 4e part 2: DC_COOP_TEST_TOWN_TALK_ROLE=2, her copy: the game's player her skin)
        static const int role = TestEnvInt("DC_COOP_TEST_TOWN_TALK_ROLE", 1);

        if (NetplayRole() != role) {
            return;
        }
        if (phase == 0 && mode == 0 && ++steps >= at) { // ED_IN_MODE_WALK
            place(true, "apart");
            phase = 1;
        } else if (phase == 1 && mode == 3) { // ED_IN_MODE_TALK
            phase = 2;
        } else if (phase == 2 && mode == 0) {
            place(false, "his talk over");
            phase = 3;
        }
        return;
    }

    // (both copies alike: her copy never saw the apart phases)
    if (phase < 4 && after > 0 && mode == 0 && CoopWorldsTicksSinceMerge() >= after) {
        place(true, "together");
        phase = 4;
    }
}

// coop-talks2: his talk (the room's talk mode, 3) beside hers, each start and end said (the tests read them). Test hook
// DC_COOP_TEST_HIS_TALK=N: N link ticks into her talk, together, Toan is put 9 before the room's first villager (the mayor),
// facing him, on both copies alike, so his next Cross talks to him beside her talk (and DC_COOP_TEST_ROOMEVENT_TOAN_WALK's
// walk stops from then).
static void HisTalkBesideHers(int mode) {
    static bool his_was = false;
    const bool  his = mode == 3 && g_talks[1].on;

    if (his != his_was) {
        his_was = his;
        std::fprintf(stderr, "coop-town: his talk %s beside hers (link tick %lld)\n", his ? "runs" : "over", (long long) NetplayLinkTick());
        std::fflush(stderr);
    }

    static const int at = TestEnvInt("DC_COOP_TEST_HIS_TALK", -1);

    if (at < 0 || g_room_event.his_talk_placed || !g_talks[1].on || mode != 0 || NetplayLinkTick() - g_room_event.talk_tick < at || Chara == nullptr ||
        Chara->frame == nullptr || CoopWorldsApart()) {
        return;
    }

    for (int i = 0; i < 10; i++) {
        CNPCharacter &villager = EdVillager[i];

        if (villager.initialized == 0 || villager.draw_enabled == 0 || villager.villager_id < 0) {
            continue;
        }

        sceVu0FVECTOR him;
        sceVu0FVECTOR rot;
        sceVu0FVECTOR toan;

        villager.GetPosition(him);
        villager.GetRotation(rot);
        // before him on the side he faces, a little round from her (she talks to him from in front of him too)
        toan[0] = him[0] + 9.0f * std::sin(rot[1] + 0.9f);
        toan[1] = him[1];
        toan[2] = him[2] + 9.0f * std::cos(rot[1] + 0.9f);
        toan[3] = 1.0f;

        const float yaw = std::atan2(him[0] - toan[0], him[2] - toan[2]);

        Chara->SetPosition(toan);
        Chara->frame->SetPosition(toan);
        Chara->SetRotation(0.0f, yaw, 0.0f);
        Chara->Step();
        g_room_event.his_talk_placed = true;
        std::fprintf(stderr, "coop: test: his talk beside hers: Toan before villager %d at (%.1f %.1f %.1f) facing %.2f, her talk %lld ticks on\n", i, toan[0],
                     toan[1], toan[2], yaw, (long long) (NetplayLinkTick() - g_room_event.talk_tick));
        std::fflush(stderr);
        return;
    }
}

// coop-p2collide test hook (DC_COOP_INDOOR_EVENT=K:N): linked and together inside a building, her character is put where
// the game's own search finds the room's event N (EventSpotNear, from where she stands), once K of her steps out of the
// room's events have passed since she was placed in it, as her walk there would; once per room. Both linked copies step it
// alike.
static void IndoorEventForTest() {
    static const char *const spec = TestEnv("DC_COOP_INDOOR_EVENT");
    static bool              done = false;
    int                      at = 0, event = -1;

    if (g_indoor.ticks == 0) {
        done = false;
    }

    if (done || spec == nullptr || std::sscanf(spec, "%d:%d", &at, &event) != 2 || g_indoor.ticks < at || EdInInfo == nullptr || g_town.body == nullptr) {
        return;
    }

    done = true;

    sceVu0FVECTOR pos;
    float         yaw = 0.0f;

    if (!EventSpotNear(g_town.pos, event, pos, &yaw)) {
        std::fprintf(stderr, "coop: test: indoor event warp: no event %d in reach in %s\n", event, EdInteriorName);
        return;
    }

    sceVu0CopyVector(g_town.pos, pos);
    g_town.velocity[0] = g_town.velocity[1] = g_town.velocity[2] = 0.0f;
    g_town.body->frame->SetRotation(0.0f, yaw, 0.0f);
    g_town.body->SetRotation(0.0f, yaw, 0.0f);
    std::fprintf(stderr, "coop: test: indoor event warp in %s at step %d: player 2 to (%.1f %.1f %.1f) facing %.2f, at event %d\n", EdInteriorName,
                 g_indoor.ticks, pos[0], pos[1], pos[2], yaw, event);
}

// coop-pt1108b test hook (DC_COOP_TEST_HIS_CLOSET=K): K link ticks into her talk, Toan is put where the game's own search finds the room's event 12 (the mayor's closet), on both copies alike;
// his Cross then opens it. (A line anchors the test's press.)
static void IndoorHisClosetForTest() {
    COOP_TEST_SWITCH(int, after, TestEnvInt("DC_COOP_TEST_HIS_CLOSET", 0));
    static bool done = false;
    CCharacter *const toan = EdExchangeInfo.player;

    if (after <= 0 || done || !g_room_event.on || !g_talks[1].on || NetplayLinkTick() - g_room_event.talk_tick < after || toan == nullptr || toan->frame == nullptr) {
        return;
    }
    done = true;

    sceVu0FVECTOR from, pos;
    float         yaw = 0.0f;

    toan->GetPosition(from);
    if (!EventSpotNear(from, 12, pos, &yaw)) {
        std::fprintf(stderr, "coop: test: his closet spot: no event 12 in reach in %s\n", EdInteriorName);
        return;
    }
    toan->SetPosition(pos);
    toan->frame->SetPosition(pos);
    toan->SetRotation(0.0f, yaw, 0.0f);
    toan->frame->SetRotation(0.0f, yaw, 0.0f);
    std::fprintf(stderr, "coop: test: his closet spot: Toan to (%.1f %.1f %.1f) at link tick %lld\n", pos[0], pos[1], pos[2], (long long) NetplayLinkTick());
    std::fflush(stderr);
}

// coop-hertalk test hook (DC_COOP_INDOOR_TALK_SPOT=K): linked and together in a room, at her Kth step in it (as
// DC_COOP_INDOOR_EVENT), she is put 9 units before the room's first villager drawn (the mayor in i01h06), facing him, as
// her walk there would; once per room. Both linked copies step it alike.
static void IndoorTalkSpotForTest() {
    // (coop-pt1108c's DC_COOP_TEST_HISTALK_CURSOR: opt8, in his talk's own step, CoopTalkStep)
    // coop-pt1108d test hook (DC_COOP_TEST_TOAN_AT_EVENT=N): with the state file back and AWAY's warp done, Toan is put where the game's search
    // finds the room's event N (its prompt over his head), once. Both linked copies step it alike.
    if (static const int toan_event = TestEnvInt("DC_COOP_TEST_TOAN_AT_EVENT", -1); toan_event > 0 && NetplayStateFileLoaded() && g_away_done && g_indoor.mode == 0 && EdInInfo != nullptr && Chara != nullptr) {
        static bool toan_done = false;

        if (!toan_done) {
            toan_done = true;

            sceVu0FVECTOR from, spot;
            float         yaw = 0.0f;

            Chara->GetPosition(from);
            if (EventSpotNear(from, toan_event, spot, &yaw)) {
                Chara->SetPosition(spot);
                Chara->frame->SetPosition(spot);
                Chara->SetRotation(0.0f, yaw, 0.0f);
                Chara->Step();
            }
            std::fprintf(stderr, "coop: test: Toan to the room's event %d at (%.1f %.1f %.1f)\n", toan_event, spot[0], spot[1], spot[2]);
            std::fflush(stderr);
        }
    }

    static const int  at = TestEnvInt("DC_COOP_INDOOR_TALK_SPOT", -1);
    // coop-fx51c: K "loaded", on her first step once a state file is put back (as DC_COOP_INDOOR_AWAY's)
    static const bool on_load = TestEnv("DC_COOP_INDOOR_TALK_SPOT") != nullptr && std::strcmp(TestEnv("DC_COOP_INDOOR_TALK_SPOT"), "loaded") == 0;
    static bool       done = false;

    if (g_indoor.ticks == 0) {
        done = false;
    }
    if (done || (on_load ? !NetplayStateFileLoaded() : at < 0 || g_indoor.ticks < at) || g_town.body == nullptr || CoopWorldsApart()) {
        return;
    }

    for (int i = 0; i < 10; i++) {
        CNPCharacter &villager = EdVillager[i];

        if (villager.initialized == 0 || villager.draw_enabled == 0 || villager.villager_id < 0) {
            continue;
        }

        sceVu0FVECTOR him;
        sceVu0FVECTOR rot;

        villager.GetPosition(him);
        villager.GetRotation(rot);
        // before him, the way he faces (his talk needs her in front of him, EdSearchNearNPC)
        g_town.pos[0] = him[0] + 9.0f * std::sin(rot[1]);
        g_town.pos[1] = him[1];
        g_town.pos[2] = him[2] + 9.0f * std::cos(rot[1]);
        g_town.pos[3] = 1.0f;
        g_town.velocity[0] = g_town.velocity[1] = g_town.velocity[2] = 0.0f;

        const float yaw = std::atan2(him[0] - g_town.pos[0], him[2] - g_town.pos[2]);

        g_town.body->frame->SetRotation(0.0f, yaw, 0.0f);
        g_town.body->SetRotation(0.0f, yaw, 0.0f);
        done = true;
        std::fprintf(stderr, "coop: test: indoor talk spot in %s at step %d: player 2 to (%.1f %.1f %.1f) facing %.2f, before villager %d at (%.1f %.1f %.1f)\n",
                     EdInteriorName, g_indoor.ticks, g_town.pos[0], g_town.pos[1], g_town.pos[2], yaw, i, him[0], him[1], him[2]);
        std::fflush(stderr);
        return;
    }
}

// coop-roomevent (Jep's 2026-10-27 playtest of main 52fe4b4: with P1 in the mayor's house P2 saw no "!" at the closet; her
// Cross there warped Toan to it, the item's scene played on his screen, both screens faded, and then her pad drove Toan):
// her Cross was put on Toan's pad, so the closet ran as his event (Toan the script's main character, its camera, window
// and fade drawn on both copies) and the town's "the pad is hers" rule (CoopWorldsP2Drives) gave her pad his port for as
// long as it ran. Now the event is hers: the script's main character is her body (her walk stands, the event moves her),
// and her pad (a game pad of her own, updated each tick as the game's is) and her bag are the game's only around the
// event's own steps (its start and EdEventMode), so nothing of hers outlives them on Toan's port. Toan walks on through it
// with his own camera (his events, talks and doors wait: the room has one event at a time); her copy draws the event's
// camera, his copy his own camera with the event's pictures, item, windows and fade drawn out of sight (they step as they
// draw). Its messages are hers (CoopWorldsPartnerSpeaks), a key item's on both. DC_COOP_MUTATE=roomevent_toan (twin): her
// Cross runs it as Toan's, on his pad, as before.
namespace {

bool RoomEventToan() {
    return Mutated("roomevent_toan");
}

// The room's event whose pickup her Cross starts from where she stands (the game's own search from her pose), or -1.
int HerRoomPickupAt(const void *program) {
    if (!g_town.visible || !g_indoor.was || CoopWorldsApart() || g_town.body == nullptr || EdInInfo == nullptr || program == nullptr ||
        !CoopWorldsPartnerMayStart(nullptr, -1)) {
        return -1;
    }

    sceVu0FVECTOR  rotation;
    ED_EVENT_PARAM param;

    g_town.body->frame->GetRotation(rotation);

    if (EdGetEvent(EdInInfo->event_points, 32, &param, g_town.pos, rotation, NowTime) == 0 || param.kind != ED_EVENT_POINT_EVENT || param.point == nullptr ||
        param.point->side <= 0) {
        return -1;
    }

    // (the script's whole program list is read for each test: the last answer is kept per event)
    static const void *checked_program = nullptr;
    static int         checked_event = -1;
    static bool        checked = false;
    const int          event = param.point->side;

    if (program != checked_program || event != checked_event) {
        checked_program = program;
        checked_event = event;
        checked = CoopWorldsPartnerRoomEvent(program, event);
    }

    return checked ? event : -1;
}

// coop-pt1106-batch (Jep's 2026-11-06 playtest, item 3: "P2 can only use the fishing board sign while P1 is out of the area"):
// outdoors, linked and together, her Cross at one of the town's map event points (Norune's fishing board, event 11) that either
// player may start as their own (CoopWorldsPartnerRoomEvent: no story, nobody taken anywhere), found by the game's own search
// from her pose, as a room's pickup (HerRoomPickupAt). DC_COOP_MUTATE=townevent_off (twin): her Cross starts none, as before.
int HerTownEventAt(const void *program) {
    if (!g_town.visible || EdInteriorFlag != 0 || CoopWorldsApart() || g_town.body == nullptr || EditMapInfo == nullptr || program == nullptr ||
        !CoopWorldsPartnerMayStart(nullptr, -1) || Mutated("townevent_off")) {
        return -1;
    }

    sceVu0FVECTOR  rotation;
    ED_EVENT_PARAM param;

    g_town.body->frame->GetRotation(rotation);

    const int found = EdGetEvent(EditMapInfo->work.events.points, EditMapInfo->event_count, &param, g_town.pos, rotation, NowTime);

    if (CoopDebug()) { // (DC_COOP_DEBUG: what her Cross outdoors found)
        std::fprintf(stderr, "worlds: player 2's Cross in town at (%.1f %.1f %.1f) facing %.2f: %s kind %d event %d\n", g_town.pos[0], g_town.pos[1], g_town.pos[2],
                     rotation[1], found != 0 ? "a point" : "no point", found != 0 ? param.kind : -1,
                     found != 0 && param.point != nullptr ? param.point->side : -1);
    }
    if (found == 0 || param.kind != ED_EVENT_POINT_EVENT || param.point == nullptr || param.point->side <= 0) {
        return -1;
    }

    return CoopWorldsPartnerRoomEvent(program, param.point->side) ? param.point->side : -1;
}

// Her agreed pad into her game pad, as the game's own is updated.
void FeedRoomEventPad() {
    InputPadState now{};

    if (g_room_event.player > 0 && NetplayPad(g_room_event.player - 1, now)) {
        PadUpdateFrom(g_room_event.pad, now);
        return;
    }
    // coop-famB: an event with no player reads no pad (its Crosses never reach it): said once an event (the twin's stop line)
    static long long said_for = -1;
    if (g_room_event.player <= 0 && said_for != g_room_event.tick) {
        said_for = g_room_event.tick;
        std::fprintf(stderr, "coop: her event or talk carries no player: no pad reaches it (link tick %lld)\n", (long long) NetplayLinkTick());
    }
}

// coop-pt1106-batch (Jep's 2026-11-06 playtest, item 2: "when P2 picks a talk option with the mayor, P1 hears the sound for
// switching between options"): the menu sounds of her event's and her talk's step (the choice cursor, its confirm and
// refuse) are hers, heard on her copy alone, as her menus' are (coop-menuse); the world's sounds in it stay everyone's.
// DC_COOP_MUTATE=hertalk_sound_everyone (twin): nobody's, played on both copies, as before.
std::optional<coop::SoundOwner> g_room_event_sound;

void RoomEventStepIn() {
    std::swap(GamePad, g_room_event.pad);
    g_room_event.bag_in = MenuSwapP2Bag();
    g_room_event.pad_in = true;
    if (!Mutated("hertalk_sound_everyone")) {
        g_room_event_sound.emplace(g_room_event.player, true); // coop-small2: the event's player's
    }
}

void RoomEventStepOut() {
    if (!g_room_event.pad_in) {
        return;
    }
    if (g_room_event.bag_in) {
        MenuSwapP2Bag();
        g_room_event.bag_in = false;
    }
    std::swap(GamePad, g_room_event.pad);
    g_room_event.pad_in = false;
    g_room_event_sound.reset();
}

// The event's main character back to the room's player, and her walk on from where the event left her.
void RoomEventOver(const char *how) {
    EdEventInfo.main_character = g_room_event.main;
    EdEventInfo.main_texture_animation = g_room_event.anime;
    EdEventInfo.main_texture_animation_count = g_room_event.anime_count;
    g_room_event.on = false;
    g_talks[1].on = false; // coop-hertalk
    g_room_event.villager = -1;
    g_room_event.ended = NetplayLinkTick(); // coop-joinfix
    g_room_event.held_up = false;           // coop-closetitem

    if (g_town.body != nullptr) {
        g_town.body->GetPosition(g_town.pos);
        g_town.pos[3] = 1.0f;
        g_town.velocity[0] = g_town.velocity[1] = g_town.velocity[2] = 0.0f;
        g_town.motion = -1; // her walk sets its motion afresh
    }

    std::fprintf(stderr, "worlds: player 2's room event %d %s at link tick %lld (%lld ticks), she stands at (%.1f %.1f %.1f)\n", g_room_event.event, how,
                 (long long) NetplayLinkTick(), (long long) (NetplayLinkTick() - g_room_event.tick), g_town.pos[0], g_town.pos[1], g_town.pos[2]);
    std::fflush(stderr);
}
} // namespace

// Test hook (DC_COOP_TEST_ROOMEVENT_SHOTS=DIR, w-indoor -Case roomevent): through her room event, every 20 ticks from its
// start, and 10, 60 and 120 ticks after its end, this copy's frame to DIR/p<player>-e<ticks>.png (-a<ticks> after), with a
// line: where Toan and she stand, the fade, the room's mode, and the camera this copy drew the room from.
static void RoomEventTestShots() {
    static const char *const dir = TestEnv("DC_COOP_TEST_ROOMEVENT_SHOTS");

    CCharacter *const toan_body = EdExchangeInfo.player; // (the room's player: Chara is the town's)

    if (dir == nullptr || toan_body == nullptr || g_town.body == nullptr) {
        return;
    }

    // (from her Cross's start of an event to the room's walk again: hers, or the twin's run as Toan's)
    static bool      running = false;
    static long long from = -1, ended = -1;
    const long long  now = NetplayLinkTick();
    long long        at = -1;
    char             kind = 'e';

    if (g_room_event.pressed >= 0 && (g_room_event.on || g_indoor.mode == 4)) {
        running = true;
        from = g_room_event.pressed;
    } else if (running) {
        running = false;
        ended = now;
        g_room_event.pressed = -1;
    }

    if (running && (now - from) % 20 == 0) {
        at = now - from;
    } else if (!running && ended >= 0 && (now - ended == 10 || now - ended == 60 || now - ended == 120)) {
        at = now - ended;
        kind = 'a';
    }

    if (at < 0) {
        return;
    }

    char          path[512];
    sceVu0FVECTOR toan;
    sceVu0FVECTOR her;
    float         colour[4] = {};

    std::snprintf(path, sizeof path, "%s/p%d-%c%lld.png", dir, NetplayLocalPlayer(), kind, at);
    NetplayTestShotTo(path);
    toan_body->GetPosition(toan);
    g_town.body->GetPosition(her);
    ::EdGetFadeColor(colour);
    const MOTION_INFO *clip = g_town.body->GetMotionInfo(g_town.body->motion_no);

    std::fprintf(stderr, "coop-roomevent: test: %c%lld p%d toan (%.1f %.1f %.1f) her (%.1f %.1f %.1f) fade %.0f mode %d view %s return %d her motion %d time %.2f (%.0f-%.0f)\n",
                 kind, at, NetplayLocalPlayer(), toan[0], toan[1], toan[2], her[0], her[1], her[2], colour[3], g_indoor.mode, g_room_event.view, (int) EdEventInfo.return_code,
                 g_town.body->motion_no, g_town.body->GetNowTime(), clip != nullptr ? clip->start : -1.0f, clip != nullptr ? clip->end : -1.0f);
    std::fflush(stderr);
}

// coop-closetitem test hook (DC_COOP_TEST_ITEM_TRACE=1): every tenth link tick (unlinked, room step) while a room event holds its item model
// (EdEventInfo.item_frame[0], the got-item model _LOAD_ITEM made), where the model is drawn (its world matrix's place), the
// frame it hangs from (_SYNC_CHARA_ITEM's reference), and the event's main character: where it stands, which way it faces,
// and whose body it is. The item in front of the getter's body is what retail shows (Toan's hand held up in front of him).
static void RoomItemTestLine() {
    static const bool on = TestEnv("DC_COOP_TEST_ITEM_TRACE") != nullptr;
    CFrame *const     item = EdEventInfo.item_frame[0];
    CCharacter *const main = EdEventInfo.main_character;
    static bool       had = false;
    static int        steps = 0;

    if (!on) {
        return;
    }
    if (item == nullptr && had) {
        had = false;
        std::fprintf(stderr, "coop-closetitem: test: item model p%d gone at link tick %lld\n", NetplayLocalPlayer(), (long long) NetplayLinkTick());
        std::fflush(stderr);
    }
    // (linked, on link ticks that are tens: the same lines on both copies)
    const bool tens = NetplayLocalPlayer() != 0 ? NetplayLinkTick() % 10 == 0 : steps++ % 10 == 0;

    if (item == nullptr || main == nullptr || main->frame == nullptr || !tens) {
        return;
    }
    had = true;

    sceVu0FMATRIX world;
    sceVu0FVECTOR body;
    sceVu0FVECTOR rotation;

    item->GetLWMatrix(world);
    main->GetPosition(body);
    main->GetRotation(rotation);

    const CFrame *hung = item->reference != 0 ? item->parent : nullptr;
    // (the body's facing: the model's forward is +z turned by its y rotation, as _ITEM_GET_CAMERA places its camera)
    const float dx = world[3][0] - body[0], dz = world[3][2] - body[2];
    const float ahead = dx * std::sin(rotation[1]) + dz * std::cos(rotation[1]);
    const float aside = dx * std::cos(rotation[1]) - dz * std::sin(rotation[1]);
    // (its head: the higher of its face frames, "kao" and "face": Xiao's face is "kao", Toan's "face")
    float head = 0.0f;

    for (const char *name : {"kao", "face"}) {
        char          copy[8];
        sceVu0FMATRIX at;

        std::snprintf(copy, sizeof copy, "%s", name);
        if (CFrame *f = main->frame->SearchFrame(copy); f != nullptr) {
            f->GetLWMatrix(at);
            head = std::max(head, at[3][1] - body[1]);
        }
    }

    std::fprintf(stderr,
                 "coop-closetitem: test: item model p%d link tick %lld at (%.1f %.1f %.1f) hung on %s%s, main character %s at (%.1f %.1f %.1f) facing %.2f: "
                 "%.1f flat from it, %.1f ahead, %.1f up, %.1f aside, head %.1f\n",
                 NetplayLocalPlayer(), (long long) NetplayLinkTick(), world[3][0], world[3][1], world[3][2], hung == nullptr ? "nothing" : "frame ",
                 hung == nullptr ? "" : hung == main->frame ? "(the body's root)" : hung->name, main == g_town.body ? "her body" : main == Chara ? "the game's player" : "another",
                 body[0], body[1], body[2], rotation[1], std::hypot(dx, dz), ahead, world[3][1] - body[1], aside, head);

    // coop-closetglow: while a system message is up, where Toan (the game's player, the room's walker) stands and whose it is.
    if (SystemMesNo > 0 && Chara != nullptr && Chara != main) {
        sceVu0FVECTOR toan;

        Chara->GetPosition(toan);
        std::fprintf(stderr, "coop-closetglow: test: walk p%d link tick %lld toan (%.1f %.1f %.1f) message %d for p%d\n", NetplayLocalPlayer(), (long long) NetplayLinkTick(),
                     toan[0], toan[1], toan[2], (int) SystemMesNo, coop::SystemMesFor());
    }

    // coop-closetglow: each event character the script hung on a frame and draws (the got-item glow, iget.chr), where it is drawn
    // against the item model: on Toan's hand both are one place (the script hangs both on his "item" frame).
    for (int i = 0; EdEventInfo.npcs != nullptr && i < EdEventInfo.npc_count && i < 16; i++) {
        CFrame *const frame = EdEventInfo.npcs[i].frame;

        if (frame == nullptr || frame->reference == 0 || frame->parent == nullptr || EdEventInfo.npc_draw[i] == 0) {
            continue;
        }
        sceVu0FMATRIX at;

        frame->GetLWMatrix(at);
        std::fprintf(stderr, "coop-closetglow: test: glow p%d link tick %lld chara %d at (%.1f %.1f %.1f) hung on frame %s, main character %s: %.1f from the item model, %.1f up from it\n",
                     NetplayLocalPlayer(), (long long) NetplayLinkTick(), i, at[3][0], at[3][1], at[3][2], frame->parent->name,
                     main == g_town.body ? "her body" : main == Chara ? "the game's player" : "another",
                     std::sqrt((at[3][0] - world[3][0]) * (at[3][0] - world[3][0]) + (at[3][1] - world[3][1]) * (at[3][1] - world[3][1]) +
                               (at[3][2] - world[3][2]) * (at[3][2] - world[3][2])),
                     at[3][1] - world[3][1]);
    }
    std::fflush(stderr);
}

// coop-talks2: her talk's own state (editloop3's talk statics and EdEventInfo's talk fields) and message window, swapped with
// the room's around her talk's start, steps and draw.
namespace dc::game {
int  EdTalkStateBytes();             // editloop3.cpp (coop-talks2)
void EdTalkStateSwap(void *saved);   // editloop3.cpp
void EdTalkWindowReset();            // editloop3.cpp
void EdInMenuToWalk();               // edit_in.cpp (coop-stage4 4c)
void EdInToTalk();                   // edit_in.cpp (coop-pt1109f)
CNPCharacter *EdNowTalkChara();      // editloop3.cpp
} // namespace dc::game

namespace {
// opt8: slot k's window afresh (the live window as it is: its message file and texture), its talk state empty.
void TalkFresh(int k) {
    std::memcpy(g_talks[k].window, static_cast<void *>(&EditMes1), sizeof(ClsMes));
    std::memset(g_talks[k].state, 0, sizeof g_talks[k].state);
}

// opt8: slot k's talk state and window exchanged with retail's live ones (twice puts everything back).
void TalkSwap(int k) {
    static_assert(sizeof g_talks[0].window == sizeof(ClsMes), "a slot's window holds a ClsMes");
    if (EdTalkStateBytes() > (int) sizeof g_talks[k].state) {
        return;
    }
    EdTalkStateSwap(g_talks[k].state);

    unsigned char *live = reinterpret_cast<unsigned char *>(static_cast<void *>(&EditMes1));

    for (std::size_t i = 0; i < sizeof(ClsMes); i++) {
        std::swap(live[i], g_talks[k].window[i]);
    }
}

void HerTalkStateFresh() {
    TalkFresh(1);
}

void HerTalkSwap() {
    TalkSwap(1);
}
} // namespace

// coop-hertalk (Jep's 2026-10-28 playtest list: "Xiao can't talk to the mayor while Toan is in the house"): the talks were
// Toan's alone (EdMoveChara's search from his pose, the room's talk mode his). Her Cross, with no pickup in reach, looks for a
// villager as his walk does (EdSearchNearNPC from her body: near, the villager facing her or turning to talk) and sets up its
// talk as his Cross does (EdTalkModeInit; its talk script, 0x100 or the villager's own). The talk then runs as hers
// (CoopTownHerTalkOn). DC_COOP_MUTATE=hertalk_off (twin): her Cross talks to nobody, as before.
// coop-machotalk (Jep's 2026-11-04 pack 8 playtest: "while P1 is in a talk with Macho, P2 can't talk to Macho", and P1's talk
// window was drawn on her screen, "like the old mayor's house bug"): all of this was a room's only; the town outdoors had no
// talk of hers, she stood through his, and his window drew on both screens. Outdoors now as in a room (editloop's walk, his
// talk and his menu): her Cross talks to a villager in reach, beside his walk, talk or menu. DC_COOP_MUTATE=towntalk_off
// (twin): outdoors her Cross talks to nobody, as before.
static int HerTalkAt() {
    const bool outdoors = EdInteriorFlag == 0;

    if (Mutated("hertalk_off") || RoomEventToan() || !g_town.visible || (outdoors ? Mutated("towntalk_off") : !g_indoor.was) || CoopWorldsApart() ||
        g_town.body == nullptr || !CoopWorldsPartnerMayStart(nullptr, -1)) {
        return -1;
    }

    const int near = EdSearchNearNPC(g_town.body, EdVillager, 10);

    // (coop-talks2: its talk set up in her own talk state as it starts, CoopTownHerTalkStartIn)
    if (near < 0 || near >= 10 || EdVillager[near].villager_id < 0) {
        return -1;
    }

    g_room_event.villager = near;
    return EdVillagerInfo[near].talk_event_no > 0 && EdVillagerInfo[near].talk_event_level != 0 ? EdVillagerInfo[near].talk_event_no : 0x100;
}

// coop-playfix (Jep's 2026-10-31 Local playtest: "if P1 is standing in front of the mayor, the mayor's name plate shows up on
// P2's screen, but it doesn't if P2 is standing near the mayor"): the villager whose name and red cursor a screen draws
// (editloop.cpp's villager cursors) is the one its own player could talk to. Retail's talk_target is EdMoveChara's search
// from the game's player (Toan's walk, both copies); together, her screen asks the same search from her body, the
// talk_target marks it writes put back (they are both copies' state), and draws only that. Draw only.
// DC_COOP_MUTATE=talkname_his (twin): his on her screen too, as before.
bool CoopTownTalkTarget(int villager) {
    if (villager < 0 || villager >= 10) {
        return false;
    }
    // opt8: no name or arrow on a talker's own screen (retail's talk mode draws none), whoever talks: his talk from its slot too.
    // DC_COOP_MUTATE=name_over_talk (twin): as before.
    if (CoopTalkLocal() && !Mutated("name_over_talk")) {
        static bool hidden_said = false;

        if (!hidden_said) {
            hidden_said = true;
            std::fprintf(stderr, "coop-talk: player %d's talk: no name or arrow over its villager on its screen\n", NetplayLocalPlayer());
        }
        return false;
    }
    // (the screen of the game's own player, the resident entry, keeps retail's; coop_actor's table, no player number here)
    if (coop::ActorLocalIndex() == 0 || CoopWorldsApart() || !g_town.visible || g_town.body == nullptr || RoomEventToan() || Mutated("talkname_his")) {
        return EdVillager[villager].talk_target != 0;
    }

    // (coop-pt1108f's rule for her talk: opt8, every talker's, above)
    s32 kept[10];

    for (int i = 0; i < 10; i++) {
        kept[i] = EdVillager[i].talk_target;
    }

    const int near = EdSearchNearNPC(g_town.body, EdVillager, 10);

    for (int i = 0; i < 10; i++) {
        EdVillager[i].talk_target = kept[i];
    }

    // (each change of her screen's target, against the game's: the w-indoor hertalk check's line)
    static int said = -2;

    if (near != said) {
        int game = -1;

        for (int i = 0; i < 10; i++) {
            game = kept[i] != 0 ? i : game;
        }
        said = near;
        std::fprintf(stderr, "coop-town: her screen's talk target villager %d (the game's %d)\n", near, game);
        std::fflush(stderr);
    }
    return near == villager;
}

// coop-p2collide: linked and together in a room, her Cross (the agreed pad, the same tick on both copies) where the game's
// own search from her pose finds one of the room's events that is a scripted pickup, no story (CoopWorldsPickupEvent: the
// closet, event 12). Story events and talks stay Toan's. DC_COOP_MUTATE=no_room_event (twin): her Cross starts nothing.
int CoopTownHerRoomEvent(const void *program, int room_mode) {
    static bool   cross_was = false;
    InputPadState pad;

    g_room_event.program = program;

    // coop-small2 (design for three): the room event's player is a number it carries, read by its pad, its sounds and its
    // script's menus (CoopTownEventPadPlayer); the town's other body is player 2's today, so its Cross is player 2's pad.
    constexpr int kTownBodyPlayer = 2;
    const bool    linked = NetplayPad(kTownBodyPlayer - 1, pad);
    const bool cross = linked && (pad.buttons & kInputCross) != 0;
    const bool pressed = cross && !cross_was;

    cross_was = cross;

    // coop-pt1109d (Jep's 11-08 tick 480, cause open: her closet started on her copy alone): all her Cross's gates, said on every copy, so two copies' lines name the one that differs.
    if (pressed && EdInteriorFlag != 0 && Linked()) {
        sceVu0FVECTOR turned = {0.0f, 0.0f, 0.0f, 0.0f};

        if (g_town.body != nullptr) {
            g_town.body->frame->GetRotation(turned);
        }
        std::fprintf(stderr, "worlds: her Cross at link tick %lld: room mode %d, event on %d, ended %lld, visible %d, indoors %d, may start %d, pad free %d, she at (%.3f %.3f %.3f) turned %.4f, clock %.4f, program %p\n",
                     (long long) NetplayLinkTick(), room_mode, g_room_event.on ? 1 : 0, (long long) g_room_event.ended, g_town.visible ? 1 : 0, g_indoor.was ? 1 : 0,
                     CoopWorldsPartnerMayStart(nullptr, -1) ? 1 : 0, CoopTownHerPadFree() ? 1 : 0, g_town.pos[0], g_town.pos[1], g_town.pos[2], turned[1], NowTime, program);
    }

    // opt8: her talk's result event (CoopTownHerTalkEnd), at its first free step: as her Cross's own event would start.
    if (g_talks[1].event > 0 && !g_room_event.on) {
        const int event = g_talks[1].event;

        g_talks[1].event = 0;
        std::fprintf(stderr, "coop-town: her talk's event %d starts as hers at link tick %lld\n", event, (long long) NetplayLinkTick());
        std::fflush(stderr);
        return event;
    }

    if (!pressed || Mutated("no_room_event") || g_room_event.on) { // coop-hertalk: (her Cross is her talk's while it runs)
        return -1;
    }

    // coop-joinfix (Jep's 2026-10-29 playtest, parked 1: her talk with the mayor started again at each Cross on its last window,
    // so she could not leave it; tonight's log: "talk ends at link tick 1336", "talks to the room's villager 0 ... at link tick
    // 1336"): the Cross that closes her talk's last window ended it earlier this tick, so it starts nothing more, as retail's
    // closing press is spent on the window. DC_COOP_MUTATE=talk_restarts (twin): it starts the talk again, as before.
    if (g_room_event.ended == NetplayLinkTick() && !Mutated("talk_restarts")) {
        return -1;
    }

    g_room_event.villager = -1;
    // (coop-talks2: a pickup, a script of the room's, only in its walk; a talk beside his talk or menu too)
    int event = room_mode == 0 ? HerRoomPickupAt(program) : -1; // ED_IN_MODE_WALK

    // coop-pt1106-batch: outdoors in the town's walk (editloop passes -2), one of the town's map events of her own
    if (event < 0 && room_mode == -2) {
        event = HerTownEventAt(program);
    }

    if (event < 0) {
        event = HerTalkAt(); // coop-hertalk: else a villager in reach
    }

    if (event < 0) {
        // coop-pt1108b: said always, with what each copy read: tick 480 of Jep's 11-08 log was her closet started on her copy alone, and no line said why his did not.
        if (g_indoor.was && g_town.visible) {
            std::fprintf(stderr, "worlds: player 2's Cross in the room at link tick %lld: no pickup or villager in reach (room mode %d, she at (%.2f %.2f %.2f))\n", (long long) NetplayLinkTick(), room_mode,
                         g_town.pos[0], g_town.pos[1], g_town.pos[2]);
        }
        return -1;
    }

    if (g_room_event.villager >= 0) {
        std::fprintf(stderr, "worlds: player 2's Cross talks to the %s's villager %d (event %d, hers) at link tick %lld\n", EdInteriorFlag != 0 ? "room" : "town",
                     g_room_event.villager, event, (long long) NetplayLinkTick());
    } else {
        std::fprintf(stderr, "worlds: player 2's Cross starts the %s's event %d (hers) at link tick %lld\n", EdInteriorFlag != 0 ? "room" : "town", event,
                     (long long) NetplayLinkTick());
    }
    std::fflush(stderr);
    g_room_event.pressed = NetplayLinkTick();

    // The twin: his Cross on the game pad's first port for this tick (the event's own press), as before.
    if (RoomEventToan()) {
        PAD_DATA *data = reinterpret_cast<PAD_DATA *>(&GamePad); // pad[0], pad[1], previous_pad[0], previous_pad[1]

        data[0].input.status.button |= kInputCross;
        data[2].input.status.button &= ~kInputCross;
        return event;
    }

    // Her own game pad: the game pad's copy (its modes), its repeats off, her pad of the tick before (no Cross) and now.
    InputPadState before = pad;

    before.buttons &= ~kInputCross;
    g_room_event.player = kTownBodyPlayer;
    g_room_event.pad = GamePad;
    g_room_event.pad.AutoRepeatOff();
    PadUpdateFrom(g_room_event.pad, before);
    PadUpdateFrom(g_room_event.pad, pad);
    return event;
}

bool CoopTownHerRoomEventIn(int event) {
    if (RoomEventToan() || g_town.body == nullptr) {
        return false;
    }

    g_room_event.on = true;
    g_room_event.event = event;
    g_room_event.tick = NetplayLinkTick();
    g_room_event.main = EdEventInfo.main_character;
    g_room_event.anime = EdEventInfo.main_texture_animation;
    g_room_event.anime_count = EdEventInfo.main_texture_animation_count;
    g_room_event.frames_hidden = 0;
    g_room_event.talk_frames_hidden = 0;
    g_room_event.motions_said = 0;
    EdEventInfo.main_character = g_town.body;
    EdEventInfo.main_texture_animation = g_town.anime;
    EdEventInfo.main_texture_animation_count = 64;
    RoomEventStepIn();
    return true;
}

void CoopTownHerRoomEventInitOut(bool started, bool talk) {
    RoomEventStepOut();

    // coop-hertalk: her villager's talk script went to the talk mode at once (no script step): her talk.
    if (!started && talk && g_room_event.villager >= 0) {
        CoopTownHerTalkBegin();
        return;
    }

    if (!started) {
        RoomEventOver("did not start");
        return;
    }

    NetplayPadProbeArm(); // coop-padswap test hook

    std::fprintf(stderr, "worlds: the room's event %d runs for player 2 at link tick %lld, her body its main character\n", g_room_event.event,
                 (long long) NetplayLinkTick());
    std::fflush(stderr);
}

// coop-closetitem (Jep's 2026-10-30 playtest, both in the mayor's house: her closet's Prickly was drawn inside her): the script
// hangs its got-item model on its main character's "item" frame (_SYNC_CHARA_ITEM) and asks for Toan's motion 6, which holds
// that hand up before his face. Her body has no such motion (coop-xiaopose: she stands), so the model hung at her lowered hand,
// inside her. Apart it was right only because her copy's event runs on Toan's own hidden body. While the script's last motion
// for her is his item held up, the model hangs from her body where his motion holds it, by her own size: his item frame stands
// at 0.78 of his face's height and 0.23 of it ahead of him (measured 2026-10-30: 11.1 up and 3.2 ahead, face 14.2), so hers at
// those parts of her head's height ("kao", the face), ahead along her facing. Put back as it was once the script lets it go
// (_RELEASE_CHARA_ITEM) or hangs it elsewhere. Both copies alike (the script's own steps). DC_COOP_MUTATE=closetitem_off (twin):
// at her hand, as before.
namespace {
constexpr int   kToanItemHeldUp = 6;
constexpr float kHeldUpHeight = 11.1f / 14.2f;
constexpr float kHeldUpAhead = 3.2f / 14.2f;

struct HeldItem {
    CFrame       *item = nullptr; // the model hung from her body, or null
    sceVu0FMATRIX local;          // its own placing before
    int           srt = 0;
} g_held_item;

// Her head's height above her feet (the face frame, else the head's top), or 0.
float HeadHeight(CCharacter *body) {
    sceVu0FVECTOR feet;
    sceVu0FMATRIX world;
    char          face[] = "kao";
    CFrame       *head = body->frame->SearchFrame(face);

    if (head == nullptr) {
        return 0.0f;
    }
    body->GetPosition(feet);
    head->GetLWMatrix(world);
    return world[3][1] - feet[1];
}

void ItemLetGo() {
    if (g_held_item.item != nullptr) {
        g_held_item.item->SetTransMatrix(g_held_item.local);
        g_held_item.item->srt = g_held_item.srt;
        g_held_item.item = nullptr;
    }
}

// coop-closetglow (Jep's 2026-11-02 playtest: her closet's Prickly before her face, its glow of light far below it): the script
// hangs more than its model on that hand. Its got-item glow (iget.chr, the event's character 5) follows the main character's
// "item" frame too (_SYNC_CHARA_CHARA(-1, "item", 5), before _SYNC_CHARA_ITEM), so on Toan's held-up hand glow and model are one
// place; on hers the glow stayed at her lowered hand. While the model is held up, every event character (and its shadow) the
// script hung on her hand hangs on this point instead: her body's own frame where the model is held (a frame of ours, never
// deleted, so nothing hung on it is left on a freed one), and back on her hand once the model is let go or gone, or her
// event ends. DC_COOP_MUTATE=closetglow_off (twin): the glow left at her hand, as before.
CFrame g_held_point;
bool   g_glow_said = false;

// Each event character's frame (and shadow) hung on `from` hung on `to` (none: let go, as _RELEASE_CHARA_CHARA does); how many.
int RehangCharas(CFrame *from, CFrame *to) {
    int moved = 0;

    for (int i = 0; from != nullptr && EdEventInfo.npcs != nullptr && i < EdEventInfo.npc_count; i++) {
        CNPCharacter &chara = EdEventInfo.npcs[i];

        for (CFrame *frame : {chara.frame, chara.shadow_frame}) {
            if (frame != nullptr && frame->reference != 0 && frame->parent == from) {
                if (to != nullptr) {
                    frame->SetReference(to);
                } else {
                    frame->DeleteReference();
                }
                moved++;
            }
        }
    }
    return moved;
}
} // namespace

static void HerItemHeldUp() {
    CFrame *const     item = EdEventInfo.item_frame[0];
    CCharacter *const body = g_town.body;

    if (item == nullptr || item != g_held_item.item) {
        g_held_item.item = nullptr; // (a new model, or none: the old one went with its event)
    }
    if (body == nullptr || body->frame == nullptr) {
        return;
    }

    char          hand_name[] = "item";
    CFrame *const hand = body->frame->SearchFrame(hand_name);
    const float   height = g_room_event.on && g_room_event.held_up && !Mutated("closetitem_off") ? HeadHeight(body) : 0.0f;

    // (in her body's own space: up its y, ahead its z, as _ITEM_GET_CAMERA places its camera before the main character)
    sceVu0FMATRIX place;

    sceVu0UnitMatrix(place);
    place[3][1] = kHeldUpHeight * height;
    place[3][2] = kHeldUpAhead * height;

    // coop-closetglow: what else the script hung on her hand (its glow, shown before the model is loaded) held up with it
    if (height > 0.0f) {
        std::snprintf(g_held_point.name, sizeof g_held_point.name, "%s", "held-up"); // (its name in the test lines)
        g_held_point.srt = 0;
        g_held_point.SetTransMatrix(place);
        g_held_point.SetReference(body->frame);
        if (const int moved = Mutated("closetglow_off") ? 0 : RehangCharas(hand, &g_held_point); moved > 0 && !g_glow_said) {
            g_glow_said = true;
            std::fprintf(stderr, "coop-closetglow: %d frame(s) her event hung on her hand held up before her face with its item at link tick %lld\n", moved,
                         (long long) NetplayLinkTick());
            std::fflush(stderr);
        }
    } else {
        RehangCharas(&g_held_point, hand); // (her hand again, as the script hung them)
    }

    if (item == nullptr || !g_room_event.on || Mutated("closetitem_off")) {
        return;
    }

    const bool hung_hers = item->reference != 0 && item->parent != nullptr && (item->parent == hand || item->parent == body->frame);

    if (!hung_hers || !g_room_event.held_up) {
        if (g_held_item.item != nullptr && (!hung_hers || item->parent == body->frame)) {
            ItemLetGo();
            if (hung_hers) {
                item->SetReference(hand); // (her hand again, as the script hung it)
            }
        }
        return;
    }

    if (height <= 0.0f) {
        return;
    }
    if (g_held_item.item == nullptr) {
        g_held_item.item = item;
        sceVu0CopyMatrix(g_held_item.local, item->local);
        g_held_item.srt = item->srt;
        std::fprintf(stderr, "coop-closetitem: her got-item model held up before her face (head %.1f: %.1f up, %.1f ahead) at link tick %lld\n", height,
                     kHeldUpHeight * height, kHeldUpAhead * height, (long long) NetplayLinkTick());
        std::fflush(stderr);
    }

    item->srt = 0;
    item->SetTransMatrix(place);
    item->SetReference(body->frame);
}

// coop-xiaopose (Jep's 2026-10-27 playtest of 2ff99b4: getting the closet's Prickly "she plays the knockdown animation on
// loop"): a room event's script asks for its main character's motions by town Toan's numbers, and her body plays the
// dungeon's. The two lists share standing, running and walking (0-2: the town's walk in ebattle.cpp, the dungeon's keyCtrl);
// above those the numbers are Toan's own (his 5, the pickup, is empty in hers; his 6, holding the item up, is her
// knock-back), and the dungeon has no got-item motion either (its chest scripts leave the player standing): she stands.
// *over: the motion is done at once (a sequence would wait for the end of one she never plays).
// DC_COOP_MUTATE=roomevent_motion_raw (twin): Toan's numbers played on her body, an empty one over at once (as before).
int CoopTownEventMotion(CCharacter *character, int motion, bool *over) {
    *over = false;

    if (!g_room_event.on || character == nullptr || character != g_town.body || motion <= 2) {
        return motion;
    }

    if (Mutated("roomevent_motion_raw")) {
        const MOTION_INFO *info = character->GetMotionInfo(motion);

        *over = info == nullptr || info->end <= info->start;
        return motion;
    }

    *over = true;
    g_room_event.held_up = motion == kToanItemHeldUp; // coop-closetitem: her got-item model held up (HerItemHeldUp)

    if (motion >= 32 || (g_room_event.motions_said & (1u << motion)) == 0) {
        g_room_event.motions_said |= motion < 32 ? 1u << motion : 0u;
        std::fprintf(stderr, "coop-xiaopose: her room event %d asks for Toan's motion %d: she stands (her motion 0)\n", g_room_event.event, motion);
    }

    return 0;
}

bool CoopTownHerRoomEventOn() {
    return g_room_event.on && !g_talks[1].on; // (coop-talks2: her talk is beside the room, not its script)
}

// coop-closetglow (Jep's 2026-11-02 playtest: "while P2 was in that closet cutscene, P1 couldn't move at all"): each step the room
// takes the pad from everything (EdSetKeyMode(0)) while a system message is up or the bag overflows: retail's walker stands
// while his own box waits. Her room event's '"Prickly" acquired.' box is hers (it waits for her Cross, SystemMesCross), so it
// stood Toan for as long as she left it up (80 s in that run). Linked and together, a message for player 2 leaves the room's
// walk his; her event's own step still runs under retail's lock (CoopTownHerRoomEventStepIn). Both copies alike (the message
// and whose it is are the same on both). DC_COOP_MUTATE=mes_locks_walk (twin): any message stands him, as before.
int CoopTownRoomKeyMode(bool message, bool overflow) {
    const bool hers = message && coop::SystemMesFor() == 2 && !Mutated("mes_locks_walk");

    return overflow || (message && !hers) ? 0 : 0xFFFF;
}

void CoopTownHerRoomEventStepIn() {
    if (!g_room_event.on) {
        return;
    }

    EdSetKeyMode(EdSystemMesCheck() != 0 || EdCheckItemOver() != 0 ? 0 : 0xFFFF); // coop-closetglow: her step under retail's lock

    // DC_COOP_DEBUG: once a second of it, where it is (link tick, the room's mode, her body, the script's return code).
    if (static int steps = 0; CoopDebug() && ++steps % 60 == 1) {
        sceVu0FVECTOR her;

        g_town.body->GetPosition(her);
        const MOTION_INFO *info = g_town.body->GetMotionInfo(g_town.body->motion_no);

        std::fprintf(stderr, "coop-roomevent: p%d her event %d step %d link tick %lld room mode %d her (%.1f %.1f %.1f) motion %d time %.2f (%.2f-%.2f by %.3f, set %.3f) return %d\n",
                     NetplayLocalPlayer(), g_room_event.event, steps, (long long) NetplayLinkTick(), g_indoor.mode, her[0], her[1], her[2], g_town.body->motion_no,
                     g_town.body->GetNowTime(), info != nullptr ? info->start : -1.0f, info != nullptr ? info->end : -1.0f, info != nullptr ? info->speed : -1.0f,
                     g_town.body->motion_speed, (int) EdEventInfo.return_code);
    }
    FeedRoomEventPad();
    RoomEventStepIn();
}

void CoopTownHerRoomEventStepOut() {
    RoomEventStepOut();
    if (g_room_event.on) {
        EdSetKeyMode(CoopTownRoomKeyMode(EdSystemMesCheck() != 0, EdCheckItemOver() != 0)); // coop-closetglow: the room's again
    }
    HerItemHeldUp(); // coop-closetitem
}

void CoopTownHerRoomEventEnd(int result) {
    if (!g_room_event.on) {
        return;
    }

    if (g_room_event.frames_hidden > 0) {
        std::fprintf(stderr, "coop-town: her room event's pictures drawn out of sight on his copy for %d frames\n", g_room_event.frames_hidden);
    }
    RoomEventOver(result == 1 ? "ends" : "ends (asked to leave or talk: not for her, ignored)");
    HerItemHeldUp(); // coop-closetglow: what was held up with her item back on her hand
    g_glow_said = false;
}

// coop-hertalk: her talk, from her villager's script's talk return (or its start): the room's talk mode is hers. She stands and
// faces the villager (EdTalkMode turns her body), her pad and bag the game's around the talk's step only, the villager's
// window on her screen only (her copy lays it out from her own camera), and Toan walks on with his own camera (his events,
// talks and doors wait, as through her room event: one talk at a time). The main character stays her body till it ends.
bool CoopTownHerTalkFromEvent(int result) {
    if (!g_room_event.on || g_room_event.villager < 0 || result != ED_EVENT_RETURN_TALK) {
        return false;
    }
    CoopTownHerTalkBegin();
    return true;
}

void CoopTownHerTalkBegin() {
    sceVu0FVECTOR toan = {};

    if (EdExchangeInfo.player != nullptr) {
        EdExchangeInfo.player->GetPosition(toan);
    }
    g_talks[1].on = true;
    g_room_event.talk_tick = NetplayLinkTick();
    g_room_event.cam_angle_from = g_town_cam.camera != nullptr ? g_town_cam.camera->GetAngle() : 0.0f; // opt8: her room camera through her talk
    g_room_event.toan_from[0] = toan[0];
    g_room_event.toan_from[1] = toan[1];
    g_room_event.toan_from[2] = toan[2];
    g_room_event.toan_far = 0.0f;
    std::fprintf(stderr, "worlds: player 2's talk with the room's villager %d runs (hers) at link tick %lld, Toan at (%.1f %.1f %.1f)\n", g_room_event.villager,
                 (long long) NetplayLinkTick(), toan[0], toan[1], toan[2]);
    std::fflush(stderr);
}

// coop-pt1108f (Jep's 11-09 playtest: P1 in first person is invisible on P2's screen): retail's viewMode is his, stepped on every copy; on her copy, whose
// screen stays on her camera, it hides nothing of his. DC_COOP_MUTATE=town_eye_hides (twin): every copy as his.
bool CoopTownEyeOnThisScreen() {
    static int said = 0;
    const int  look = EdCheckViewMode() != 0 ? 1 : 0;
    const bool here = look != 0 && (coop::ActorLocalIndex() == 0 || CoopWorldsApart() || !g_town.visible || Mutated("town_eye_hides"));

    if (look != said) { // (the k-first-person town row's line)
        said = look;
        std::fprintf(stderr, "coop-town: his first-person look %s at link tick %lld, player %d's screen %s him\n", look != 0 ? "on" : "off", (long long) NetplayLinkTick(),
                     NetplayLocalPlayer(), here ? "hides" : "shows");
    }
    return here;
}

bool CoopTownHerTalkOn() {
    return g_room_event.on && g_talks[1].on;
}

CCharacter *CoopTownHerTalkBody() {
    return g_town.body;
}

// (the walk's mover asks once a tick it would run; the test trace counts the asks her talk held and let through)
static int g_villagers_held = 0;
static int g_villagers_let = 0;

// opt8 (Jep 11-09: the world pauses only when every player is in a pause state): the villagers' walk holds while every
// present player talks or is in a menu (CoopTalkWorldHeld), not for one talker; retail's own rule keeps each talker's villager
// still (it stops within 20 of a player: CoopTownNearestPlayer). Replaces coop-pt1109b's hold of every villager through her talk.
bool CoopTownVillagersHeld() {
    const bool talk = EdInteriorFlag == 0 && CoopTalkAny();
    const bool holds = CoopTalkWorldHeld();

    if (talk) {
        (holds ? g_villagers_held : g_villagers_let)++;
    }
    return holds;
}

// opt8: retail's villager stops within 20 of the game's player (EdMoveVillager, from EdExchangeInfo.player): so a villager talking
// to another player walked on through the talk. Each villager stops for the nearest player (her body, shown in the town with him).
// DC_COOP_MUTATE=villagers_stop_for_one (twin): the game's player alone, as before.
float CoopTownNearestPlayer(const float *villager, float from_game_player) {
    if (!Linked() || CoopWorldsApart() || !g_town.visible || g_town.body == nullptr || Mutated("villagers_stop_for_one")) {
        return from_game_player;
    }
    sceVu0FVECTOR at;

    g_town.body->GetPosition(at);
    return std::min(from_game_player, std::sqrt((at[0] - villager[0]) * (at[0] - villager[0]) + (at[1] - villager[1]) * (at[1] - villager[1]) +
                                                (at[2] - villager[2]) * (at[2] - villager[2])));
}

CCameraFollow *CoopTownHerTalkCamera() {
    if (EdInteriorFlag != 0 || g_indoor.on || !g_talks[1].on || !g_town_cam.ready || g_town_cam.camera == nullptr || Mutated("hertalk_nocam")) {
        return nullptr;
    }
    return g_town_cam.camera;
}

namespace {
bool          g_talk_view_set = false; // her copy: the view set to her camera around the talk's step
sceVu0FMATRIX g_talk_view_was;
sceVu0FVECTOR g_talk_eye_was;
} // namespace

void CoopTownHerTalkStepIn(CCamera *room) {
    HerTalkSwap(); // coop-talks2: her talk's state and window

    // She stands through it, breathing (as Toan through his talk), on both copies.
    g_town.body->SetMotion(0, 0);
    g_town.body->SetVelocity(CVector3_f_(0.0f, 0.0f, 0.0f));
    g_town.body->Step();
    g_town.body->ShadowStep();
    g_town.body->ClothStep(0);

    if (EdExchangeInfo.player != nullptr) {
        sceVu0FVECTOR toan;

        EdExchangeInfo.player->GetPosition(toan);
        g_room_event.toan_far = std::max(g_room_event.toan_far, std::hypot(toan[0] - g_room_event.toan_from[0], toan[2] - g_room_event.toan_from[2]));
    }

    FeedRoomEventPad();
    RoomEventStepIn();

    // Test hook (DC_COOP_TEST_HERTALK_CURSOR=1; coop-pt1106-batch, w-indoor-hertalk): her talk's choice cursor sound once, on
    // its first step, as the talk's choice menu plays it on her Up or Down (DialogNpcStep's ComMenuSePlay), under the step's
    // sound owner: the fixture's talk has no choice menu of its own.
    static bool cursor_played = false;

    if (static const bool cursor = TestEnvOn("DC_COOP_TEST_HERTALK_CURSOR"); cursor && !cursor_played) {
        cursor_played = true;
        std::fprintf(stderr, "coop-town: test: her talk's choice cursor sound at link tick %lld\n", (long long) NetplayLinkTick());
        ComMenuSePlay(MENU_SOUND_CURSOR);
    }

    // Her copy lays the window out where her screen shows the villager and her (the talk step reads the view).
    g_talk_view_set = false;
    if (NetplayLocalPlayer() == 2 && g_town_cam.ready && g_town_cam.camera != nullptr && room != nullptr && !Mutated("hertalk_shown")) {
        sceVu0FMATRIX view;
        sceVu0FVECTOR eye;

        room->GetCameraMatrix(g_talk_view_was);
        room->GetPos(g_talk_eye_was);
        g_town_cam.camera->GetCameraMatrix(view);
        g_town_cam.camera->GetPos(eye);
        MGSetViewMatrix(view, eye);
        g_talk_view_set = true;
    }
}

void CoopTownHerTalkStepOut() {
    if (CCameraFollow *camera = CoopTownHerTalkCamera(); camera != nullptr) { // coop-pt1109b: retail's TalkCamera Step(1), on hers
        camera->Step(1);
        if (static long long said_for = -1; said_for != g_room_event.talk_tick && NetplayLinkTick() >= g_room_event.talk_tick + 30) { // (once a talk, as the camera has moved in: x-town-talk's line)
            sceVu0FVECTOR eye;

            camera->GetPos(eye);
            said_for = g_room_event.talk_tick;
            std::fprintf(stderr, "coop-town: her outdoor talk camera is %.0f from her at link tick %lld\n", DistVector(eye, g_town.pos), (long long) NetplayLinkTick());
            std::fflush(stderr);
        }
    }
    RoomEventStepOut();
    if (g_talk_view_set) {
        MGSetViewMatrix(g_talk_view_was, g_talk_eye_was);
        g_talk_view_set = false;
    }
    HerTalkSwap(); // coop-talks2: his talk's state and window back
}

// coop-talks2 (Jep, 2026-10-29, on "his talks and scripted events wait through hers": "do it"): her talk has its own talk
// state and message window beside the room's (his), so both talk at once, and his scripted events run through hers. Her
// Cross at a villager starts it beside his walk, talk or menu (not over his script: the room has one script engine): the
// talk set up and its talk script run at once in her state (StartIn, edit_in's EdEventInit, StartOut), her body the script's
// main character and her pad and bag the game's for it. A talk script that runs on in steps is not hers beside the room.
void CoopTownHerTalkStartIn() {
    g_room_event.main = EdEventInfo.main_character;
    g_room_event.anime = EdEventInfo.main_texture_animation;
    g_room_event.anime_count = EdEventInfo.main_texture_animation_count;

    // Her window afresh: the room's window as it is (its message file and texture), closed for a talk's first message.
    HerTalkStateFresh();
    HerTalkSwap();
    EdTalkWindowReset();

    if (EdTalkModeInit(&EdVillager[g_room_event.villager], -1) == 0) {
        std::fprintf(stderr, "worlds: player 2's talk with villager %d: the game would not set it up\n", g_room_event.villager);
    }

    EdEventInfo.main_character = g_town.body;
    EdEventInfo.main_texture_animation = g_town.anime;
    EdEventInfo.main_texture_animation_count = 64;
    RoomEventStepIn(); // (her game pad as her Cross left it, CoopTownHerRoomEvent)
}

void CoopTownHerTalkStartOut(bool talks, int runs) {
    RoomEventStepOut();
    EdEventInfo.main_character = g_room_event.main;
    EdEventInfo.main_texture_animation = g_room_event.anime;
    EdEventInfo.main_texture_animation_count = g_room_event.anime_count;
    HerTalkSwap();

    if (!talks) {
        std::fprintf(stderr, "worlds: player 2's talk with villager %d did not start (its talk script %s)\n", g_room_event.villager,
                     runs != 0 ? "runs on in steps: not hers beside the room, cleared" : "went no further");
        std::fflush(stderr);
        g_room_event.villager = -1;
        return;
    }

    g_room_event.on = true;
    g_room_event.event = 0x100;
    g_room_event.tick = NetplayLinkTick();
    g_room_event.frames_hidden = 0;
    g_room_event.talk_frames_hidden = 0;
    g_room_event.motions_said = 0;
    CoopTownHerTalkBegin();
}

bool CoopTownHerTalkPending() {
    return g_room_event.villager >= 0 && !g_room_event.on;
}

// coop-talks2 twin DC_COOP_MUTATE=talks_one: his talk waits for hers (his walk sets none up), as coop-hertalk had it.
bool CoopTownHisTalkWaits() {
    return CoopTownHerTalkOn() && Mutated("talks_one");
}

// Test hook (DC_COOP_TEST_HERTALK_SHOP=N, x-town-hershop): a plain talk's end as a keeper's: retail's shop result, shop N (no fixture has a keeper's talk at hand).
// opt8 test hook (DC_COOP_TEST_HERTALK_EVENT=N, x-town-hertalk-villagers -Event): her plain talk's end as one whose choice runs event N (retail's
// result 4): no fixture's villager has such a talk at hand.
int CoopTownTestTalkEvent(int result, int *event_no) {
    COOP_TEST_SWITCH(int, event, TestEnvInt("DC_COOP_TEST_HERTALK_EVENT", 0));

    if (event <= 0 || result != 1) {
        return result;
    }
    *event_no = event;
    std::fprintf(stderr, "coop-town: test: the talk ends as one whose choice runs event %d\n", event);
    return 4;
}

int CoopTownTestTalkShop(int result) {
    COOP_TEST_SWITCH(int, shop, TestEnvInt("DC_COOP_TEST_HERTALK_SHOP", 0));

    if (shop <= 0 || result != 1) {
        return result;
    }
    EdSetShopNo(shop);
    std::fprintf(stderr, "coop-town: test: the talk ends as a keeper's, shop %d\n", shop);
    return 2;
}

void CoopTownHerTalkEnd(int result, int event_no) {
    if (!CoopTownHerTalkOn()) {
        return;
    }
    result = CoopTownTestTalkShop(result);
    if (EdInteriorFlag != 0 && g_town_cam.camera != nullptr) { // opt8 (w-indoor -FollowCam's line)
        std::fprintf(stderr, "coop-talk: player 2's room camera through her talk: turned %.4f\n", std::remainder(g_town_cam.camera->GetAngle() - g_room_event.cam_angle_from, 6.2831853f));
    }
    std::fprintf(stderr, "worlds: through her talk Toan walked up to %.1f from where he stood as it began\n", g_room_event.toan_far);
    if (g_room_event.talk_frames_hidden > 0) {
        std::fprintf(stderr, "coop-town: her talk's window drawn out of sight on his copy for %d frames\n", g_room_event.talk_frames_hidden);
    }
    result = CoopTownTestTalkEvent(result, &event_no);
    // coop-p1parts (Jep 11-08): a keeper's talk of hers ends in her own shop, as his (retail's result: 2 a shop, 3 the other kind; shop no set by the talk on every copy).
    const bool shop = result == 2 || result == 3;
    const bool opens = shop && CoopTownHerShopOpens(result == 2 ? ED_MENU_SHOP : ED_MENU_UNK_4);
    // opt8: retail's result 4 (the talk's choice runs its event, TalkMode's RunEvent): hers runs as her own event, through her own event's start
    // (CoopTownHerRoomEvent hands it on at the next free step: her body its main character, her camera, nothing of his stopped), when it is one
    // a player may start alone (CoopWorldsPartnerRoomEvent: no story, nobody taken anywhere). A story event from her talk is said and not run:
    // story events stay the game's player's (coop-worlds). DC_COOP_MUTATE=hertalk_event_dropped (twin): never run, as before.
    const bool event = result == 4 && event_no > 0;
    const bool runs = event && !Mutated("hertalk_event_dropped") && CoopWorldsPartnerRoomEvent(g_room_event.program, event_no);

    if (runs) {
        g_talks[1].event = event_no;
    }
    RoomEventOver(opens ? "talk ends (her shop opens)" : shop ? "talk ends (its shop is not opened for her)" : runs ? "talk ends (its event runs as hers)"
                  : event ? "talk ends (its event is not run for her)" : "talk ends");
}

// opt8 (Jep 11-09 night: one talk path for every player; review shared-state-review.md, class M): together, the game's player's
// talk no longer puts the town or the room into retail's talk mode, which every copy steps and which made each co-op rule
// around it a patch on "the town is in its talk mode for her too" (she walked through it, her clock and chests drawn back, its
// window drawn out of sight on her copy, its sounds kept off hers). On the tick a talk reaches retail's mode together (his
// Cross, a script's talk return, or a merge into his talk apart) it moves into his slot and the town or room walks: every
// talk then steps from its slot (CoopTalkStep) and draws from it (CoopTalkDrawBegin), whoever's it is. Apart a copy is one
// player and keeps retail's mode. DC_COOP_MUTATE=talk_town_mode (twin): his talk stays the town's mode, as before.
bool CoopTalkOn(int k) {
    return k >= 0 && k < coop::kMaxPlayers && g_talks[k].on;
}

bool CoopTalkAny() {
    for (const TalkSlot &slot : g_talks) {
        if (slot.on) {
            return true;
        }
    }
    return false;
}

// This copy's own player talks (its screen is on its talk: no clock, chests or villager names, as retail's talk mode).
bool CoopTalkLocal() {
    return CoopTalkOn(coop::ActorLocalIndex());
}

// The game's player (the resident) talks from its slot: its walk stands, its menu, pause and Georama do not open. Its walk (EdMoveChara) is also
// where its stick turns its town camera, so the camera holds through its talk as in retail's talk mode (x-town-talk-saved's check).
// DC_COOP_MUTATE=talk_walker_free (twin): the walk runs through the talk.
bool CoopTalkHoldsWalker() {
    return g_talks[0].on && !Mutated("talk_walker_free");
}

// Jep 11-09: talking (or a menu) is a pause state for its player; the world (the clock, the villagers' walk) holds only when
// every present player is in one. DC_COOP_MUTATE=world_held_by_one (twin): one talker holds it, as retail's one player.
// DC_COOP_MUTATE=world_never_held (twin): never held, as the town before (her talk aside of his).
bool CoopTalkWorldHeld() {
    if (!Linked() || CoopWorldsApart() || Mutated("world_never_held")) {
        return false;
    }
    if (Mutated("world_held_by_one") && CoopTalkAny()) {
        return true;
    }
    bool any = false;

    for (int k = 0; k < coop::kMaxPlayers; k++) {
        // (who plays this town: the game's player, the second walker while she is shown in it; the floor's actor table for more)
        const bool present = k == 0 || (k == 1 ? g_town.visible && g_town.body != nullptr : coop::ActorPresent(k));

        if (!present) {
            continue;
        }
        if (!g_talks[k].on && !coop::PlayerInMenu(k + 1)) {
            return false;
        }
        any = any || g_talks[k].on;
    }
    return any; // (every player in a menu alone: the menus' own rules, as before)
}

// Retail's live talk into slot k: its state and window to the slot, the live window closed for the next talk.
static void TalkTake(int k) {
    TalkFresh(k);
    TalkSwap(k);
    EdTalkWindowReset();
    g_talks[k].on = true;
    g_talks[k].tick = NetplayLinkTick();
}

// Once a tick, before the town's (editloop) or the room's (edit_in) mode switch, on every copy alike.
static void CarriedTalkBecomesHers(); // (below, coop-pt1109a)

void CoopTalkIntoSlots() {
    if (!Linked() || CoopWorldsApart() || Mutated("talk_town_mode")) {
        return;
    }
    // A carried talk (her merge into her own talk apart, CoopTownSwapDeferred: her skin the game's player in retail's talk mode) goes into her
    // slot here as well, before retail's talk mode would step it together (CoopTownSwapDeferTick did it later in the tick).
    if (CoopTownSwapDeferred()) {
        if ((EdInteriorFlag == 0 ? ::GameMode == ED_MODE_TALK : g_indoor.mode == 3) && !Mutated("dialog_carried")) {
            CarriedTalkBecomesHers();
        }
        return;
    }
    const bool outdoors = EdInteriorFlag == 0;

    if (outdoors ? ::GameMode != ED_MODE_TALK : g_indoor.mode != 3) { // ED_IN_MODE_TALK
        return;
    }
    if (g_talks[0].on) { // (a second talk of the same player: cannot be, said)
        std::fprintf(stderr, "coop-talk: player 1's talk already runs in its slot; retail's talk mode left as it is\n");
        return;
    }
    CNPCharacter *villager = EdNowTalkChara();

    TalkTake(0);
    if (outdoors) {
        ::GameMode = ED_MODE_WALK;
    } else {
        EdInMenuToWalk(); // (the room's own mode, this file's mirror with it)
        g_indoor.mode = 0; // ED_IN_MODE_WALK
    }
    std::fprintf(stderr, "coop-talk: player 1's talk with villager %d runs from its slot at link tick %lld (%s walks)\n",
                 villager != nullptr ? (int) (villager - EdVillager) : -1, (long long) NetplayLinkTick(), outdoors ? "the town" : "the room");
    std::fflush(stderr);
}

// One step of slot k's talk (the caller asks only for a slot that is on): retail's talk step for that player's body, its pad,
// bag and sounds, its camera. The game's player (k 0) is the resident: retail's TalkMode stand, its sounds on its copy. Player 2
// (the town's second walker today) through her step's pad, bag, sound owner and view (CoopTownHerTalkStepIn). Returns
// EdTalkMode's result, which the caller hands to CoopTalkOver.
int CoopTalkStep(int k, CCharacter *resident, CCamera *room, CCameraFollow *camera, int view_mode, int *choice) {
    // opt8: retail's TalkMode keeps every villager drawn whole through a talk outdoors (alpha_step_override 128, near_camera off: the talk
    // camera comes in close and would fade them), for whoever talks; on every copy alike, as TalkMode ran. DC_COOP_MUTATE=talk_look_off (twin).
    if (EdInteriorFlag == 0 && !Mutated("talk_look_off")) {
        for (int i = 0; i < 10; i++) {
            EdVillager[i].alpha_step_override = 128;
            EdVillager[i].near_camera = false;
        }
        if (static long long said = -1; said != g_talks[k].tick) {
            said = g_talks[k].tick;
            std::fprintf(stderr, "coop-talk: player %d's talk keeps the villagers drawn whole (retail's TalkMode) at link tick %lld\n", k + 1, (long long) NetplayLinkTick());
        }
    }
    if (k != 0) {
        CoopTownHerTalkStepIn(room);
        const int result = EdTalkMode(CoopTownHerTalkBody(), CoopTownHerTalkCamera(), 0, choice);
        CoopTownHerTalkStepOut();
        return result;
    }

    TalkSwap(0);
    std::optional<coop::SoundOwner> sound;

    if (!Mutated("talk_sound_everyone")) {
        sound.emplace(1, true); // (the talk's choice sounds on its talker's copy alone)
    }
    // coop-pt1108c test hook (DC_COOP_TEST_HISTALK_CURSOR=1, x-town-stayermenu -TalkSound): his talk's choice cursor sound once, as
    // DC_COOP_TEST_HERTALK_CURSOR (the fixture's talk has no choice menu of its own), on every copy in the lockstep's step of it.
    if (static const bool talk_cursor = TestEnvOn("DC_COOP_TEST_HISTALK_CURSOR"); talk_cursor) {
        static bool played = false;

        if (!played) {
            played = true;
            std::fprintf(stderr, "coop-town: test: his talk's choice cursor sound at link tick %lld\n", (long long) NetplayLinkTick());
            ComMenuSePlay(MENU_SOUND_CURSOR);
        }
    }
    resident->SetMotion(0, 0); // retail's talk stand (TalkMode, edit_in's talk mode)
    resident->SetVelocity(CVector3_f_(0.0f, 0.0f, 0.0f));
    resident->Step();
    resident->ShadowStep();
    resident->ClothStep(0);

    const int result = EdTalkMode(resident, camera, view_mode, choice);

    sound.reset();
    TalkSwap(0);
    return result;
}

// Slot k's talk ended with result (EdTalkMode's). For the game's player retail's result rule is the caller's (2 its shop, 3 the
// other one, 4 its event): returns result. Player 2's ends through CoopTownHerTalkEnd (her own shop); 0 returned.
int CoopTalkOver(int k, int result, int choice) {
    if (k != 0) {
        CoopTownHerTalkEnd(result, choice);
        return 0;
    }
    result = CoopTownTestTalkShop(result);
    g_talks[0].on = false;
    std::fprintf(stderr, "coop-talk: player 1's talk ends (result %d, choice %d) at link tick %lld, %lld ticks\n", result, choice, (long long) NetplayLinkTick(),
                 (long long) (NetplayLinkTick() - g_talks[0].tick));
    std::fflush(stderr);
    return result;
}

// opt8: each slot's window drawn on every copy (it steps as it draws, so every copy must), shown on its own player's copy
// alone, out of sight on the others. Replaces coop-mayorhouse's and coop-hertalk's two hides for the two players.
// DC_COOP_MUTATE=talk_shown (twin): shown on every copy (hertalk_shown: player 2's alone, the older twin).
namespace {
gfx::TextureHandle g_slot_hidden = gfx::kNullTexture;
gfx::TextureHandle g_slot_previous = gfx::kNullTexture;
int                g_slot_hidden_frames[coop::kMaxPlayers] = {};
long long          g_slot_rect_said[coop::kMaxPlayers] = {-1, -1, -1, -1, -1, -1};

bool HideDraw() {
    if (g_slot_hidden == gfx::kNullTexture) {
        g_slot_hidden = gfx::CreateRenderTarget(640, 480, true);
    }
    g_slot_previous = gfx::CurrentRenderTarget();
    gfx::SetRenderTarget(g_slot_hidden);
    return true;
}
} // namespace

bool CoopTalkDrawBegin(int k) {
    TalkSwap(k);

    // Test hook (DC_COOP_TEST_TALK_RECT=1): each talk's window, once its text is laid out, on every copy: where its text is.
    if (static const bool rect_log = TestEnvOn("DC_COOP_TEST_TALK_RECT"); rect_log && g_slot_rect_said[k] != g_talks[k].tick && EditMes1.text_width > 0) {
        g_slot_rect_said[k] = g_talks[k].tick;
        std::fprintf(stderr, "coop-town: test: talk window text at (%d %d) size (%d %d), bubble %d, link tick %lld, player %d's copy\n", (int) EditMes1.text_x,
                     (int) EditMes1.text_y, (int) EditMes1.text_width, (int) EditMes1.text_height, (int) EditMes1.fukidashi, (long long) NetplayLinkTick(),
                     NetplayLocalPlayer());
        std::fflush(stderr);
    }

    if (k == coop::ActorLocalIndex() || Mutated("talk_shown") || (k != 0 && Mutated("hertalk_shown"))) {
        return false;
    }
    if (g_slot_hidden_frames[k]++ == 0) {
        std::fprintf(stderr, "coop-talk: player %d's talk window drawn out of sight on player %d's copy (link tick %lld)\n", k + 1, NetplayLocalPlayer(),
                     (long long) NetplayLinkTick());
        std::fflush(stderr);
    }
    if (k != 0) {
        g_room_event.talk_frames_hidden++; // (her talk's end line, the tests')
    }
    return HideDraw();
}

void CoopTalkDrawEnd(int k, bool hidden) {
    if (hidden) {
        gfx::SetRenderTarget(g_slot_previous);
    }
    TalkSwap(k);
}

// Each tick: a slot's talk over says how long its window was drawn out of sight on this copy (the tests' line).
static void TalkHiddenSaid() {
    for (int k = 0; k < coop::kMaxPlayers; k++) {
        if (!g_talks[k].on && g_slot_hidden_frames[k] > 0) {
            std::fprintf(stderr, "coop-talk: player %d's talk window drawn out of sight on player %d's copy for %d frames\n", k + 1, NetplayLocalPlayer(),
                         g_slot_hidden_frames[k]);
            std::fflush(stderr);
            g_slot_hidden_frames[k] = 0;
        }
    }
}

// opt8: the clock and the town's chests (DrawSysGra) are drawn on every copy (the draw steps its own state), out of sight on a
// copy whose own player talks: retail's talk mode draws none for its talker. Replaces coop-pt1109c's CoopTownHudInHisTalk.
// DC_COOP_MUTATE=talk_hud_shown (twin): drawn on the talker's screen too.
// Each step of his talk (editloop): his town camera's angle and distance; at its end, how far they moved through it (x-town-talk-saved's line).
void CoopTalkCameraTrace(float angle, float distance, bool ends) {
    static long long talk = -2;
    static float     angle_from = 0.0f, distance_from = 0.0f;

    if (talk != g_talks[0].tick) {
        talk = g_talks[0].tick;
        angle_from = angle;
        distance_from = distance;
    }
    if (ends) {
        std::fprintf(stderr, "coop-talk: player 1's town camera through his talk: turned %.4f, distance %.2f to %.2f\n", std::remainder(angle - angle_from, 6.2831853f),
                     distance_from, distance);
    }
}

// opt8: retail's talk mode draws no shadows (editloop's draw, shadow_on); a talker's own screen as retail, the others' as in walking.
// Draw only. DC_COOP_MUTATE=talk_look_off (twin): shadows on the talker's screen too.
bool CoopTalkShadowsOff() {
    if (!CoopTalkLocal() || Mutated("talk_look_off")) {
        return false;
    }
    if (static long long said = -1; said != g_talks[coop::ActorLocalIndex()].tick) {
        said = g_talks[coop::ActorLocalIndex()].tick;
        std::fprintf(stderr, "coop-talk: player %d's screen draws no shadows through its own talk (link tick %lld)\n", NetplayLocalPlayer(), (long long) NetplayLinkTick());
    }
    return true;
}

bool CoopTalkHudHiddenBegin() {
    if (!CoopTalkLocal() || Mutated("talk_hud_shown")) {
        return false;
    }
    static int said = 0;

    if (said++ % 120 == 0) {
        std::fprintf(stderr, "coop-talk: player %d's screen draws no clock or chests through its own talk (link tick %lld)\n", NetplayLocalPlayer(),
                     (long long) NetplayLinkTick());
    }
    return HideDraw();
}

void CoopTalkHudHiddenEnd(bool hidden) {
    if (hidden) {
        gfx::SetRenderTarget(g_slot_previous);
    }
}

unsigned char *CoopTownTalksBytes(std::size_t *size) {
    *size = sizeof g_talks;
    return reinterpret_cast<unsigned char *>(&g_talks[0]);
}

// coop-pt1109f (Jep 11-09: one player leaving by a door must not end the other's dialog, on either copy, either player): the reverse of CarriedTalkBecomesHers. Her talk beside the walk
// lives in her own talk state, with her body as its main character, because together the game's player is Toan. The split makes her skin the game's player on her copy, so that state and
// body have no one left to run beside, and the split simply ended it. It is the game's one talk now: her state to the game's, on retail's own talk mode (retail's step, draw and end), as
// a talk she starts apart. His copy has left: her talk has no place there, so it is dropped there alone. DC_COOP_MUTATE=door_ends_dialog (twin): ends on both copies, as before.
static void HerTalkAtTheSplit(bool walks) {
    // (DC_COOP_MUTATE=door_ends_his_dialog (twin): her door closes his talk on his copy: what the rows' check is there to catch.)
    if (Mutated("door_ends_his_dialog") && NetplayRole() == 1 && CoopWorldsStayedHere()) {
        if (EdInteriorFlag == 0 && ::GameMode == ED_MODE_TALK) {
            ::GameMode = ED_MODE_WALK;
        } else if (EdInteriorFlag != 0 && g_indoor.mode == 3) {
            EdInMenuToWalk();
            g_indoor.mode = 0;
        }
        g_talks[0].on = false; // (opt8: his talk in its slot)
    }
    // opt8: player 1's talk in its slot, the same rule: on his copy that stayed, it goes on as its own talk on retail's mode; on the copy
    // that left it is dropped.
    if (g_talks[0].on) {
        if (NetplayRole() != 1 || !CoopWorldsStayedHere() || Mutated("door_ends_dialog")) {
            g_talks[0].on = false;
            std::fprintf(stderr, "coop-talk: player 1's talk ends on player %d's copy (the pair split)\n", NetplayLocalPlayer());
        } else if (walks) {
            TalkSwap(0);
            g_talks[0].on = false;
            if (EdInteriorFlag == 0) {
                ::GameMode = ED_MODE_TALK;
                NowCamera = (CCamera *) &TalkCamera;
            } else {
                EdInToTalk();
                g_indoor.mode = 3; // ED_IN_MODE_TALK
            }
            std::fprintf(stderr, "coop-talk: player 1's talk goes on as this copy's own talk (the pair split) at link tick %lld\n", (long long) NetplayLinkTick());
        }
        std::fflush(stderr);
    }
    if (!g_talks[1].on) {
        return;
    }
    if (NetplayRole() != 2 || !CoopWorldsStayedHere() || Mutated("door_ends_dialog")) { // (the role: apart the link is down, no local player)
        RoomEventStepOut();
        RoomEventOver("talk ends (the pair split)");
        return;
    }
    if (!walks) { // (the room or the town still on the door's own mode: retail's talk starts from its walk)
        return;
    }
    RoomEventStepOut();
    HerTalkSwap();
    if (EdInteriorFlag == 0) {
        ::GameMode = ED_MODE_TALK;
    } else {
        EdInToTalk();
        g_indoor.mode = 3; // ED_IN_MODE_TALK
    }
    RoomEventOver("goes on as this copy's own talk (the pair split)");
}

// coop-pt1109f test hook (DC_COOP_TEST_TALK_DOOR=her|his, x-talk-door): linked and together, once a state file is put back, the talker (her, or Toan) put 9 before the first villager there;
// each of the talk's steps then said: when it runs (the row's Crosses wait on it), and how long it ran after the split on the copy that stayed.
static void TalkDoorForTest() {
    static const char *const spec = TestEnv("DC_COOP_TEST_TALK_DOOR");

    if (spec == nullptr || !NetplayStateFileLoaded() || Chara == nullptr || Chara->frame == nullptr || g_town.body == nullptr || g_town.body->frame == nullptr) {
        return;
    }
    const bool her = std::strcmp(spec, "her") == 0;
    // (opt8: the game's player's talk, from its slot together or on retail's mode)
    const bool retail = g_talks[0].on || (EdInteriorFlag == 0 ? ::GameMode == ED_MODE_TALK : CoopTownIndoorMode() == 3);
    static bool      placed = false, said = false, apart_said = false, over = false;
    static int       apart_steps = 0; // (apart the link's tick stands: its own count)

    if (!placed && Linked() && !CoopWorldsApart() && (EdInteriorFlag == 0 ? ::GameMode == ED_MODE_WALK : CoopTownIndoorMode() == 0)) {
        for (int i = 0; i < 10 && !placed; i++) {
            CNPCharacter &villager = EdVillager[i];

            if (villager.initialized == 0 || villager.villager_id < 0) {
                continue;
            }
            villager.draw_enabled = true;

            sceVu0FVECTOR him;
            sceVu0FVECTOR rot;
            sceVu0FVECTOR to;

            villager.GetPosition(him);
            villager.GetRotation(rot);
            to[0] = him[0] + 9.0f * std::sin(rot[1]);
            to[1] = him[1];
            to[2] = him[2] + 9.0f * std::cos(rot[1]);
            to[3] = 1.0f;

            const float yaw = std::atan2(him[0] - to[0], him[2] - to[2]);

            if (her) {
                sceVu0CopyVector(g_town.pos, to);
                g_town.velocity[0] = g_town.velocity[1] = g_town.velocity[2] = 0.0f;
                g_town.body->frame->SetRotation(0.0f, yaw, 0.0f);
                g_town.body->SetRotation(0.0f, yaw, 0.0f);
            } else {
                Chara->SetPosition(to);
                Chara->frame->SetPosition(to);
                Chara->SetRotation(0.0f, yaw, 0.0f);
                Chara->Step();
            }
            placed = true;
            std::fprintf(stderr, "coop: test: talk door: %s before villager %d at (%.1f %.1f %.1f), link tick %lld\n", her ? "player 2" : "Toan", i, to[0], to[1], to[2], (long long) NetplayLinkTick());
        }
    }
    if (!said && !CoopWorldsApart() && (her ? CoopTownHerTalkOn() : retail)) {
        said = true;
        std::fprintf(stderr, "coop: test: talk door: the talk runs at link tick %lld\n", (long long) NetplayLinkTick());
    }
    if (CoopWorldsApart() && CoopWorldsStayedHere() && !over) {
        if (retail && !apart_said) {
            apart_said = true;
            std::fprintf(stderr, "coop: test: talk door: the talk goes on apart on player %d's copy at link tick %lld\n", NetplayRole(), (long long) NetplayLinkTick());
        } else if (retail) {
            apart_steps++;
        } else if (apart_said) {
            over = true;
            sceVu0FVECTOR at;

            Chara->GetPosition(at);
            std::fprintf(stderr, "coop: test: talk door: the talk ended after %d ticks apart, player %d walks from (%.1f %.1f %.1f)\n", apart_steps, NetplayRole(), at[0], at[1], at[2]);
        }
    }
    std::fflush(stderr);
}

CCamera *CoopTownHerRoomEventCamera(CCamera *room) {
    return g_town_cam.ready && g_town_cam.camera != nullptr && !RoomEventToan() ? g_town_cam.camera : room;
}

const ED_EVENT_INFO *CoopTownRoomEventForToan(const ED_EVENT_INFO *event) {
    static ED_EVENT_INFO drawn;

    if (!CoopTownHerRoomEventOn() || event == nullptr) { // (coop-pt1108b: beside her talk the event is his, its player his)
        return event;
    }

    // Her event's player is her: Toan is drawn as he walks.
    drawn = *event;
    drawn.player_draw = 1;
    drawn.player_shadow_draw = 1;
    return &drawn;
}

// coop-mayorhide (Jep's 2026-10-28 Steam playtest of 0585aff: "the mayor vanishes while P2 opens the closet"): a room
// event's _INITIALIZE (EdInitEventParam) wipes every one of the room's people (the villager buffer, their texture
// animations, each EdVillager Initialize'd and its textures dropped); the closet's script loads the mayor again only at its
// end (_CLEAR_VILLAGER_BUFF, _LOAD_IN_VILLAGER). Alone, Toan's event camera looks at the closet and nobody sees the room
// empty; together the other player walks the room through it and sees the mayor gone for its ~10 s. A room event that is
// hers (CoopWorldsPartnerRoomEvent, which she may start), or a scripted pickup of Toan's (CoopWorldsPickupEvent) with her
// in the room, keeps them: neither loads anybody, so the room's people and their buffer are not the event's (the arrival
// 128 and other story events still start from an empty room). Both copies decide alike (the
// together state and the script). program, event_no: the script running and its event (a system event's, or a room's
// arrival in another room's script, is not the room's own walk-time script: EdEventData). Called from the script's
// _INITIALIZE (editloop3.cpp), not the room's load. DC_COOP_MUTATE=room_people_wiped (twin): wiped, as before.
static bool RoomPointEvent(int event_no) {
    if (EdInInfo == nullptr) {
        return false;
    }

    for (const ED_EVENT_POINT &point : EdInInfo->event_points) {
        if (point.enabled != 0 && point.event_type == ED_EVENT_POINT_EVENT && point.side == event_no) {
            return true;
        }
    }
    return false;
}

bool CoopTownRoomKeepsPeople(const void *program, int event_no) {
    if (Mutated("room_people_wiped") || program == nullptr || program != EdEventData || !g_indoor.was) {
        return false;
    }

    const bool hers = g_room_event.on;
    // (Toan's: one his Cross started at one of the room's event points from its walk, on this tick or the one before; not the
    // room's arrival 128, whose first run is the mayor's story with its item pickups)
    const bool both_here = g_town.visible && !CoopWorldsApart() && NetplayLinkTick() - g_indoor.walked <= 2 && RoomPointEvent(event_no);

    if (!hers && !(both_here && CoopWorldsPickupEvent(program, event_no))) {
        return false;
    }

    std::fprintf(stderr, "coop-mayorhide: the room keeps its people through event %d (%s) at link tick %lld\n", event_no, hers ? "player 2's" : "player 1's, player 2 here",
                 (long long) NetplayLinkTick());
    std::fflush(stderr);
    return true;
}


static void CoopTownDoorFadeSaid(); // coop-doorfade

// coop-doorprompt (Jep's 2026-10-27 playtest: at Norune's cave door player 1's Square put the Use Item window on both screens,
// player 2 could not use the key there, and player 1's key faded her screen as he went in): in a
// linked town together, either player's Square at the cave door (the door point to "dungeon", whose event 130 opens the
// game's Use Item window as a game-wide event on both copies) opens that player's own picker (coop_interact's, drawn on
// their copy alone); the world and the other player go on. His Square is taken off the game's pad for this tick, so the
// event 130 his walk queued in reach does not open the window. Both copies on the same tick (the agreed pads).
// DC_COOP_MUTATE=doorshared (twin): his Square opens the game's window, hers nothing, as before.
static bool AtCaveDoor(const ED_EVENT_PARAM &param) {
    return param.kind == ED_EVENT_POINT_DOOR && param.point != nullptr && std::strcmp(param.point->destination, "dungeon") == 0;
}

// coop-doorfix (Jep's 2026-10-30 Local playtest: P2 could not open the keyring at the cave door while P1 was in the mayor's
// house): apart, the copy's own player is the game's walker (TownResident), and the door's event 130 ran its Use Item window
// over that player's bag alone (her copy: the ring beside the game's pack, so no cave key listed). Now the walker's Square
// at the door opens their own picker apart too, read from the game's pad (no agreed pads apart). DC_COOP_MUTATE=
// cavepick_linked_only (twin): linked and together only, as before.
static void CaveDoorSquares() {
    static bool   his_was = false;
    static bool   her_was = false;
    InputPadState one{};
    InputPadState two{};
    PAD_DATA     *data = reinterpret_cast<PAD_DATA *>(&GamePad); // pad[0], pad[1], previous_pad[0], previous_pad[1]

    const bool linked = NetplayPad(0, one) && NetplayPad(1, two);
    const bool apart = CoopWorldsApart() && !CaveDoorFixOff("cavepick_linked_only");
    const bool his = apart ? (data[0].input.status.button & kInputSquare) != 0 : linked && (one.buttons & kInputSquare) != 0;
    const bool hers = linked && (two.buttons & kInputSquare) != 0;
    const bool his_press = his && !his_was;
    const bool her_press = hers && !her_was;
    const int  walker = TownResident(); // the game's walker: player 1, or apart this copy's player

    his_was = his;
    her_was = hers;

    // (coop-menucross: linked and together, whatever has her pad: his Square is his)
    if ((!CoopWorldsLinkedTogether() && !apart) || EdInteriorFlag != 0 || Mutated("doorshared")) {
        return;
    }

    // The tick his picker closes (MenuTick, at the pad's read) his pad reaches the game again with the Cross or Circle that
    // closed it: taken off here too, or the event 130 his walk queued would take it (the door's own entry once the key set
    // flag 1, on the copy that stays as well).
    static bool his_open_was = false;
    const bool  his_open = coop::TownDoorPickOpenFor(walker);

    if (his_open_was && !his_open) {
        data[0].input.status.button &= ~(kInputCross | kInputCircle | kInputTriangle);
    }
    his_open_was = his_open;

    if (his_press && EdMoveCharaInfo.event_ready != 0 && AtCaveDoor(EdMoveCharaInfo.param) && !coop::TownDoorPickOpenFor(walker) && !CoopTownCaveActs(walker)) {
        data[0].input.status.button &= ~kInputSquare;
        if (!CoopWorldsCaveDoorRefused(walker)) { // coop-doorgate: B1's story needs both players
            coop::OpenTownDoorPickFor(walker);
        }
    }

    if (apart) {
        return; // (her partner is not in this copy's town apart)
    }

    sceVu0FVECTOR pos;
    sceVu0FVECTOR rot;
    ED_EVENT_PARAM param;

    if (her_press && !coop::TownDoorPickOpenFor(2) && !CoopTownCaveActs(2) && !coop::PlayerInMenu(2, coop::kActorMenuTown) && !CoopP2TownPaused() && !CoopP2Building() && CoopTownHerPadFree() &&
        EditMapInfo != nullptr &&
        CoopTownPartnerPose(pos, rot) && EdGetEvent(EditMapInfo->work.events.points, EditMapInfo->event_count, &param, pos, rot, NowTime) != 0 &&
        AtCaveDoor(param) && !CoopWorldsCaveDoorRefused(2)) {
        coop::OpenTownDoorPickFor(2);
    }
}

// coop-p2marks (Jep's 2026-10-27 playtest: no "!" for player 2 at the cave door, though her Square works): Toan's comes from
// the door's event 130, which his walk runs in reach and which draws it over him (_DRAW_EXCLAMATION_MARK, EdDrawSysCursor).
// Hers is drawn over her where her Square opens her picker (CaveDoorSquares: linked and together, outdoors, nothing of hers
// open), at his height above the feet (3 + her body's height), on both copies as his is. Draw-only. DC_COOP_MUTATE=nomarks
// (twin): none.
// coop-pt1108f (Jep's 11-09 playtest: no "!" over her head at the fishing sign, as he has at his): where her Cross would start one of the town's map events
// as her own (HerTownEventAt's own test). DC_COOP_MUTATE=sign_nomark (twin): none.
static bool HerSignMark(const ED_EVENT_PARAM &param) {
    static bool said = false;
    const bool  there = !Mutated("sign_nomark") && !g_room_event.on && g_room_event.program != nullptr && param.kind == ED_EVENT_POINT_EVENT && param.point != nullptr &&
                       param.point->side > 0 && !CoopWorldsApart() && CoopWorldsPartnerRoomEvent(g_room_event.program, param.point->side);

    if (there && !said) {
        said = true;
        std::fprintf(stderr, "coop-town: her \"!\" over her head at the town's event %d\n", param.point->side);
    }
    return there;
}

void CoopTownDrawPartnerMark(CFrame *mark) {
    static float  bob = 0.0f;
    sceVu0FVECTOR pos;
    sceVu0FVECTOR rot;
    ED_EVENT_PARAM param;

    // coop-roomevent (Jep's 2026-10-27 playtest: no "!" for her at the mayor's closet): indoors, where her Cross starts one of
    // the room's pickups (CoopTownHerRoomEvent's own test), while the room walks. DC_COOP_MUTATE=roomevent_nomark (twin): none.
    const bool drawn = !coop::PartnerMarkHeld() && CoopWorldsPartnerMayStart(nullptr, -1) && !coop::TownDoorPickOpenFor(2) && !coop::PlayerInMenu(2, coop::kActorMenuTown) &&
                       !CoopP2TownPaused() && !CoopP2Building() &&
                       (EdInteriorFlag == 0 ? EditMapInfo != nullptr && CoopTownPartnerPose(pos, rot) &&
                                                  EdGetEvent(EditMapInfo->work.events.points, EditMapInfo->event_count, &param, pos, rot, NowTime) != 0 &&
                                                  (AtCaveDoor(param) || HerSignMark(param))
                                            : CoopTownIndoorWalks() && !g_room_event.on && !Mutated("roomevent_nomark") &&
                                                  HerRoomPickupAt(g_room_event.program) >= 0 && CoopTownPartnerPose(pos, rot));

    if (drawn) {
        pos[1] += 3.0f + g_town.body->body_height + 0.5f * sinf(bob);
        bob += 0.1f;
        if (!(bob <= PI_SHORT)) {
            bob -= TWO_PI;
        }
    }

    coop::DrawPartnerMarkAt(mark, pos, drawn);
}

// coop-menucross (Jep's 2026-10-27 playtest: Xiao's Triangle in front of the cave door, then her Cross into her bag's page:
// that Cross also used the door, and its "A big key hole" box came up on both screens): her pad is the world's only while
// nothing of hers has it, as Toan's menu keeps his Cross from his walk: not her menu or her pause (nor on the tick either
// lets go), her Georama, her town wheel (nor on the tick it closes); her picker at the cave door is
// CoopWorldsPartnerMayStart's own. Her doors, her cave door and her room pickups ask it there, her Square at the cave door
// here. DC_COOP_MUTATE=menucross (twin): her Cross reaches them whatever has her pad, and the shut cave door's event runs
// for both (CoopTownHerCaveDoorShut), as before.
bool CoopTownHerPadFree() {
    if (Mutated("menucross")) {
        return true;
    }

    return !CoopP2TownMenuHoldsPad() && !CoopP2Building() && !coop::TownWheelHoldsPad() && !CoopTownCaveActs(2) && // (coop-doorfix: nor her act)
           !coop::TownNoteHolds(2) && (coop::TownNoteHeldAfter(2) & kInputCross) == 0; // (coop-cavehold: nor her door message, nor its Cross)
}

// DC_COOP_DEBUG: her Cross pressed while something of hers has her pad (the menucross test's line), with what has it.
static void HerHeldCrossTrace() {
    static bool   cross_was = false;
    InputPadState pad;

    const bool cross = NetplayPad(1, pad) && (pad.buttons & kInputCross) != 0;
    const bool pressed = cross && !cross_was;

    cross_was = cross;

    if (!pressed || !CoopDebug() || !CoopWorldsLinkedTogether() || (CoopTownHerPadFree() && !coop::TownDoorPickHeldFor(2))) {
        return;
    }

    std::fprintf(stderr, "coop-town: player 2's Cross kept from the world at link tick %lld (menu %d pause %d georama %d wheel %d picker %d)\n",
                 (long long) NetplayLinkTick(), (int) CoopP2TownMenuOn(), (int) CoopP2TownPaused(), (int) CoopP2Building(),
                 (int) coop::TownWheelHoldsPad(), (int) coop::TownDoorPickHeldFor(2));
    std::fflush(stderr);
}

// coop-menucross: her Cross at Norune's cave door while it is shut (the door's flag clear: no cave key used yet). The door's
// event 130 only shows its message then ("A big key hole", message 300), but as the game's event: both copies stop in the
// event mode and both screens show it. It is her note instead, on her copy alone, and the town plays on. True when it
// took her Cross. DC_COOP_MUTATE=cavenote_event (twin), and menucross (all as before): the door's event runs for both.
// coop-doorfix (Jep's 2026-10-30 Local playtest: Toan's Cross at the door was taken too): any player's Cross there, together
// (CoopWorldsDungeonEventAlone for the game's walker's). DC_COOP_MUTATE=cavecross_event (twin): the walker's Cross runs the
// door's event for both, as before.
bool CoopTownCaveDoorShutNote(int player) {
    int       flag = -1;
    const int mes = EdEventData != nullptr ? coop::TownDoorShutMessage(EdEventData, 130, &flag) : -1;

    if (mes < 0 || SaveData == nullptr || SaveData->GetGameFlag(flag) != 0 || Mutated("cavenote_event") || Mutated("menucross")) {
        return false;
    }

    std::fprintf(stderr, "coop-town: player %d's Cross at the shut cave door (flag %d clear) at link tick %lld: message %d as their note, no event\n", player,
                 flag, (long long) NetplayLinkTick(), mes);
    std::fflush(stderr);
    return coop::TownNotePrompt(player, mes);
}

bool CoopTownHerCaveDoorShut() {
    return CoopTownCaveDoorShutNote(2);
}

// coop-doorfix: the game walker's Cross at the cave door together (its event 130 starting from his walk, or from her Cross put
// on his pad): shut, that player's note; opened, that player's walk in beside the game (CoopTownCaveEntry). True when it took
// the Cross (no event runs).
bool CoopTownCaveCross(int player, bool open) {
    if (!open) {
        return !CaveDoorFixOff("cavecross_event") && CoopTownCaveDoorShutNote(player);
    }

    if (!CoopTownCaveEntry(player)) {
        return false;
    }

    std::fprintf(stderr, "coop-town: player %d's Cross at the opened cave door: the walk in beside the game\n", player);
    std::fflush(stderr);
    return true;
}

// coop-cavehold: each town tick, both copies, a door's waiting message on its player's agreed pad; and while a note is up,
// where its player stands every 20 link ticks (the x-cave-door cross row's line: held in place).
static void DoorNotesStep() {
    for (int player = 1; player <= 2; player++) {
        InputPadState pad{};

        NetplayPad(player - 1, pad);
        coop::StepTownNote(player, pad, true);
        if (coop::TownNoteShown(player) && NetplayLinkTick() % 20 == 0 && CaveActor(player) != nullptr) {
            sceVu0FVECTOR at;

            CaveActorPos(player, at);
            std::fprintf(stderr, "coop-town: door message up for player %d at (%.1f %.1f) at link tick %lld (waits %d)\n", player, at[0], at[2],
                         (long long) NetplayLinkTick(), (int) coop::TownNoteHolds(player));
            std::fflush(stderr);
        }
    }
}

// coop-uinotice: the member pad 2 plays in town (the area notices' name).
int CoopTownPartnerMember() {
    return TownMember();
}

// coop-townchest (Jep's 2026-10-30 playtest item 25, again on LAN pack 8 2026-11-04: "P2 can't open the small red chests in
// town while P1 is in the same zone, but can once P1 leaves"): a town chest is an item box on a Georama part, and only Toan's
// walk (EdMoveChara) opened one. Apart her copy's player is the game's walker, so hers opened there; together her Cross
// (PartnerDoor) took doors alone. Now, linked and together outdoors, her Cross (the agreed pad, both copies on the same tick)
// where the game's own search from her pose finds a shut box opens it as Toan's walk does: the box's flag, its item into her
// own bag (the party's when hers keeps no such item), the lid and its sound. Its message is hers and timed (Toan's waits for a
// Cross, which would hold her pad). DC_COOP_MUTATE=no_partner_box (twin): her Cross opens nothing, as before.
static bool PartnerOpenBox(const ED_EVENT_PARAM &param) {
    if (param.kind != ED_EVENT_POINT_ITEM_BOX || param.point == nullptr || Mutated("no_partner_box")) {
        return false;
    }

    const int     item = param.point->side;
    const int     item_no = TransWepNo(item);
    const int     refused = coop::PartnerKeeps(item_no) ? coop::PartnerCheckItemGet(item_no) : EdCheckGetItem(item);
    sceVu0FVECTOR at = {param.position[0], param.position[1], param.position[2], 1.0f};

    {
        const coop::present::Scope placed(coop::present::At{at}, COOP_PRESENT_HERE); // heard by distance from the box
        SndSePlay(SE_BOX_OPEN, -1, 0);
    }

    if (refused != 0) {
        const coop::present::Scope hers(coop::present::For{2}, COOP_PRESENT_HERE);
        SetSystemMes(refused == 1 ? 0x48 : 0x51, 0x96, MES_POS_BOTTOM, 0, nullptr, nullptr); // DontGetItemMes's, timed
        std::fprintf(stderr, "coop: partner's item box refused item %d (%d)\n", item, refused);
        std::fflush(stderr);
        return true;
    }

    EdSetMapFlag(param.point->completion_flag, 1);
    int attach = -1;

    if (GetAddAttachItem(item) != 0) {
        attach = rand() % 3 + 1; // (both copies, the same tick: the agreed generator)
    }

    const bool bag = attach <= 0 && coop::PartnerGetItem(item_no, -1, false);

    if (!bag) {
        EdGetItem(item, param.point->linked_value, attach);
    }

    {
        const coop::present::Scope hers(coop::present::For{2}, COOP_PRESENT_HERE);
        ItemGetMes(item, attach, 0x96, 0);
    }

    sceVu0FVECTOR rotation = {param.rotation[0], param.rotation[1], param.rotation[2], param.rotation[3]};

    EdSetOpenItemBox(at, rotation);
    std::fprintf(stderr, "coop: partner opens the item box at (%.0f %.0f %.0f): item %d into %s, flag %d set\n", at[0], at[1], at[2], item,
                 bag ? "her bag" : "the party's", param.point->completion_flag);
    std::fflush(stderr);
    return true;
}

// Her Cross outdoors in town, linked and together, nothing of hers holding her pad.
static void PartnerTownBox() {
    static bool   cross_was = false;
    InputPadState pad;
    sceVu0FVECTOR pos;
    sceVu0FVECTOR rot;

    const bool linked = NetplayPad(1, pad);
    const bool cross = linked && (pad.buttons & kInputCross) != 0;
    const bool pressed = cross && !cross_was;

    cross_was = cross;

    if (!pressed || EdInteriorFlag != 0 || EditMapInfo == nullptr || !CoopWorldsPartnerMayStart(nullptr, -1) || !CoopTownPartnerPose(pos, rot) ||
        coop::TownDoorPickOpenFor(2) || coop::PlayerInMenu(2, coop::kActorMenuTown) || CoopP2TownPaused() || CoopP2Building()) {
        return;
    }

    ED_EVENT_PARAM param;

    if (EdGetEvent(EditMapInfo->work.events.points, EditMapInfo->event_count, &param, pos, rot, NowTime) != 0) {
        PartnerOpenBox(param);
    }
}

// coop-townchest test hook (DC_COOP_TOWN_BOX=K, w-town-chest): K steps after the pair is together in town, she is put in
// reach of the town's item box nearest her that is still shut, as a walk there would leave her: the box's place as the
// game's own search works it out (EdGetEvent over that point alone, its reach widened so the search hands back where the box
// stands), then her spot 6 in front of it on its own facing, checked with the game's search from there. Once. Both linked
// copies alike.
static void TownBoxWarpForTest() {
    static const int  at = TestEnvInt("DC_COOP_TOWN_BOX", 0);
    // (DC_COOP_TOWN_WARP_PRESS=1: K on the press clock instead, alike on both copies after a fixture's load)
    static const bool on_press = TestEnvOn("DC_COOP_TOWN_WARP_PRESS");
    static int        steps = 0;
    static bool       done = false;

    if (at <= 0 || done || EdInteriorFlag != 0 || EditMapInfo == nullptr || g_town.body == nullptr || !g_town.visible || !CoopWorldsLinkedTogether()) {
        return;
    }
    if (on_press ? (steps = (int) NetplayTestPressClock()) < at : ++steps < at) {
        return;
    }

    done = true;

    ED_EVENT_POINT *points = EditMapInfo->work.events.points;
    float           best = -1.0f;
    sceVu0FVECTOR   best_at = {};
    sceVu0FVECTOR   best_rot = {};
    int             best_no = -1;
    int             boxes = 0;
    bool            best_opened = false;
    int             best_cost = 0;
    CDngStatusData *status = SaveData != nullptr ? SaveData->GetDngStatus() : nullptr;

    for (int i = 0; i < EditMapInfo->event_count; i++) {
        if (points[i].event_type != ED_EVENT_POINT_ITEM_BOX) {
            continue;
        }

        boxes++;
        ED_EVENT_POINT one = points[i];
        ED_EVENT_PARAM param;
        sceVu0FVECTOR  rot = {0.0f, 0.0f, 0.0f, 0.0f};
        // (the search takes a box only from 20 below it to 1 above: a scan of heights around hers)
        bool found = false;
        // (a box the save has opened already is searched as shut: the test opens its flag again below, if it is the one)
        const bool opened = one.completion_flag > 0 && EdGetMapFlag(one.completion_flag) != 0;
        // (and one deeper in Norune's story than the save is: the test takes the save that deep, below; Norune is dungeon 0)
        const bool deeper = MapNo == 0 && status != nullptr && one.minimum_progress > status->floor_reached[0] + 1;

        one.trigger_range[0] = 1.0e9f;
        one.completion_flag = 0;
        if (deeper) {
            one.minimum_progress = 0;
        }
        for (int dy = -200; dy <= 200 && !found; dy += 10) {
            sceVu0FVECTOR level = {g_town.pos[0], g_town.pos[1] + dy, g_town.pos[2], 1.0f};

            found = EdGetEvent(&one, 1, &param, level, rot, NowTime) != 0;
        }
        std::fprintf(stderr, "coop: test: town box %d: enabled %d flag %d %s, part %d %s, frame %s, progress %d, item %d: %s\n", i, points[i].enabled,
                     points[i].completion_flag, opened ? "set" : "clear", points[i].parts_no,
                     points[i].parts_no >= 0 && EdExchangeInfo.ground != nullptr && EdExchangeInfo.ground->GetPartsObject(points[i].parts_no) != nullptr ? "placed" : "not placed",
                     points[i].frame == nullptr ? "none" : (points[i].frame->attr.draw_on & 1) != 0 ? "drawn" : "hidden", points[i].minimum_progress, points[i].side,
                     found ? "found" : "not found");
        if (!found) {
            continue; // not placed, hidden, or not out at this time
        }
        // (a box as the save has it goes before one the test must change, the fewest changes first)
        const int cost = (opened ? 1 : 0) + (deeper ? 1 : 0);

        if (best_no < 0 || cost < best_cost) {
            best = -1.0f;
        } else if (cost > best_cost) {
            continue;
        }

        const float dx = param.position[0] - g_town.pos[0];
        const float dz = param.position[2] - g_town.pos[2];
        const float dist = dx * dx + dz * dz;

        if (best < 0.0f || dist < best) {
            best = dist;
            best_no = i;
            best_opened = opened;
            best_cost = cost;
            sceVu0CopyVector(best_at, param.position);
            sceVu0CopyVector(best_rot, param.rotation);
        }
    }

    if (best_no < 0) {
        std::fprintf(stderr, "coop: test: town box warp: no shut item box (%d boxes in %d event points)\n", boxes, EditMapInfo->event_count);
        std::fflush(stderr);
        return;
    }

    if (best_opened) { // (both copies on the same tick, as the box's own open sets it)
        EdSetMapFlag(points[best_no].completion_flag, 0);
        std::fprintf(stderr, "coop: test: town box %d: its flag %d cleared for the test (the save had opened it)\n", best_no, points[best_no].completion_flag);
    }
    if (MapNo == 0 && status != nullptr && points[best_no].minimum_progress > status->floor_reached[0] + 1) { // (both copies alike)
        std::fprintf(stderr, "coop: test: town box %d: dungeon 0's deepest floor reached %d -> %d for the test (it needs progress %d)\n", best_no,
                     (int) status->floor_reached[0], points[best_no].minimum_progress - 1, points[best_no].minimum_progress);
        status->floor_reached[0] = (s8) (points[best_no].minimum_progress - 1);
    }

    // In front of the box on its own facing, 6 out (its reach is wider), level with it; then the game's search from there.
    sceVu0FVECTOR  spot = {best_at[0] + 6.0f * sinf(best_rot[1]), best_at[1], best_at[2] + 6.0f * cosf(best_rot[1]), 1.0f};
    sceVu0FVECTOR  face = {0.0f, best_rot[1] + PI, 0.0f, 0.0f};
    ED_EVENT_PARAM check;
    const bool     finds = EdGetEvent(points, EditMapInfo->event_count, &check, spot, face, NowTime) != 0 && check.kind == ED_EVENT_POINT_ITEM_BOX;

    sceVu0CopyVector(g_town.pos, spot);
    g_town.velocity[0] = g_town.velocity[1] = g_town.velocity[2] = 0.0f;
    g_town.body->frame->SetRotation(0.0f, face[1], 0.0f);
    g_town.body->SetRotation(0.0f, face[1], 0.0f);
    std::fprintf(stderr, "coop: test: town box warp at step %d: player 2 to (%.1f %.1f %.1f) by the box at (%.1f %.1f %.1f) (point %d of %d boxes, flag %d), %s\n",
                 steps, spot[0], spot[1], spot[2], best_at[0], best_at[1], best_at[2], best_no, boxes, points[best_no].completion_flag,
                 finds ? "the game finds it from there" : "NOT found from there");
    std::fflush(stderr);
}

// Test hook (DC_COOP_TEST_AT_MAP_EVENT=W; coop-pt1106-batch, x-town-sign): on the town's first walk step (a fixture's load, linked
// and together, or a copy alone for W=1), the players W names ("1" Toan, "2" her, "12" both) are put in reach of the town's
// first map event point (Norune's fishing board, event 11), each 6 out in front of it on its own facing, Toan 3 to its left
// and she 3 to its right, facing it: the game's search from each spot finds it (said). Once; both linked copies alike.
// (Both named: the first at the board, the other 20 to its side facing it, out of its reach: "21" her reading with him by.)
static void AtMapEventForTest() {
    static const char *const who = TestEnv("DC_COOP_TEST_AT_MAP_EVENT");
    static bool              done = false;

    if (who == nullptr || done || EditMapInfo == nullptr || EdInteriorFlag != 0 || Chara == nullptr || ::GameMode != ED_MODE_WALK) {
        return;
    }

    const bool toan = std::strchr(who, '1') != nullptr;
    const bool her = std::strchr(who, '2') != nullptr;

    if (her && (g_town.body == nullptr || !CoopWorldsLinkedTogether())) {
        return;
    }
    // (a row's fixture first, when it has one: its load puts the town back, and the pond's plot after it, PlacePartTestTick)
    if (TestEnv("DC_STATE_LOAD_AT") != nullptr && !NetplayStateFileLoaded()) {
        return;
    }
    static int tries = 0;

    ED_EVENT_POINT *points = EditMapInfo->work.events.points;
    int             at = -1;

    for (int i = 0; points != nullptr && i < EditMapInfo->event_count && at < 0; i++) {
        at = points[i].event_type == ED_EVENT_POINT_EVENT && points[i].side > 0 ? i : -1;
    }
    if (at < 0) {
        done = true;
        std::fprintf(stderr, "coop: test: at the map event: none in this town\n");
        return;
    }

    // (the search hands back where the point stands with its reach widened, as TownBoxWarpForTest's)
    ED_EVENT_POINT one = points[at];
    ED_EVENT_PARAM param;
    sceVu0FVECTOR  none = {0.0f, 0.0f, 0.0f, 0.0f};
    sceVu0FVECTOR  from = {};
    bool           found = false;

    Chara->GetPosition(from);
    one.trigger_range[0] = 1.0e9f;
    one.completion_flag = 0;
    for (int dy = -200; dy <= 200 && !found; dy += 10) {
        sceVu0FVECTOR level = {from[0], from[1] + dy, from[2], 1.0f};

        found = EdGetEvent(&one, 1, &param, level, none, NowTime) != 0;
    }
    if (!found && ++tries < 120) {
        return; // (its part may come down a tick after the load: both copies look alike each tick)
    }
    done = true;
    if (!found) {
        std::fprintf(stderr, "coop: test: at the map event: point %d (event %d) not found (part %d %s, frame %s, progress %d/%d, hours %.2f-%.2f, now %.2f)\n", at,
                     points[at].side, points[at].parts_no,
                     points[at].parts_no >= 0 && EdExchangeInfo.ground != nullptr && EdExchangeInfo.ground->GetPartsObject(points[at].parts_no) != nullptr ? "placed" : "not placed",
                     points[at].frame == nullptr ? "none" : (points[at].frame->attr.draw_on & 1) != 0 ? "drawn" : "hidden", points[at].minimum_progress,
                     points[at].secondary_progress, points[at].start_time, points[at].end_time, NowTime);
        return;
    }

    const float   yaw = param.rotation[1];
    const float   side[2] = {std::cos(yaw), -std::sin(yaw)}; // (the point's own left-right)
    sceVu0FVECTOR face = {0.0f, yaw + PI, 0.0f, 0.0f};

    for (int p = 1; p <= 2; p++) {
        if ((p == 1 && !toan) || (p == 2 && !her)) {
            continue;
        }
        // (both named: the first is at the board, the other 20 to its side, out of its reach, in the area)
        // ("1&2": both in its reach, Toan 3 to its left and she 3 to its right)
        const bool     both_reach = std::strchr(who, '&') != nullptr;
        const float    off = both_reach ? (p == 1 ? -3.0f : 3.0f) : toan && her && p != who[0] - '0' ? 20.0f : 0.0f;
        sceVu0FVECTOR  spot = {param.position[0] + 6.0f * std::sin(yaw) + off * side[0], param.position[1], param.position[2] + 6.0f * std::cos(yaw) + off * side[1], 1.0f};
        ED_EVENT_PARAM check;
        const bool     finds = EdGetEvent(points, EditMapInfo->event_count, &check, spot, face, NowTime) != 0 && check.kind == ED_EVENT_POINT_EVENT;

        if (p == 1) {
            Chara->SetPosition(spot);
            Chara->frame->SetPosition(spot);
            Chara->SetRotation(0.0f, face[1], 0.0f);
            Chara->Step();
        } else {
            sceVu0CopyVector(g_town.pos, spot);
            g_town.velocity[0] = g_town.velocity[1] = g_town.velocity[2] = 0.0f;
            g_town.body->frame->SetRotation(0.0f, face[1], 0.0f);
            g_town.body->SetRotation(0.0f, face[1], 0.0f);
        }
        std::fprintf(stderr, "coop: test: at the map event: %s to (%.1f %.1f %.1f) by point %d (event %d) at (%.1f %.1f %.1f), link tick %lld, %s\n",
                     p == 1 ? "Toan" : "player 2", spot[0], spot[1], spot[2], at, points[at].side, param.position[0], param.position[1], param.position[2],
                     (long long) NetplayLinkTick(), off != 0.0f && !both_reach ? (finds ? "IN its reach" : "out of its reach") : finds ? "the game finds it from there" : "NOT found from there");
    }
    coop::InventoryTestSeed(); // coop-scriptmenus: DC_COOP_BAGSEED (x-town-sign: the fishing rod the board's script asks for), both copies alike
    if (static const int points = TestEnvInt("DC_COOP_TEST_FISHING_POINTS", -1); points >= 0 && SaveData != nullptr) {
        SaveData->SetFishingPoint(points); // (x-town-sign -Menu exchange: points to exchange)
        std::fprintf(stderr, "coop: test: fishing points %d\n", points);
    }
    std::fflush(stderr);
}

// coop-soakfix3: a door's place in the world (its point is its part's), by the game's own search with its reach widened, as
// AtMapEventForTest finds the board: what a row's door warp (DC_WORLDS_TEST_PARTNER_WARP / _GATE_WARP) takes. Point i of the town's
// map (DC_COOP_TEST_EVENT_POINTS lists them all); plot >= 0: only a door on that plot (PlacePartTestTick, after it puts the plot down).
void CoopTownDoorWorldForTest(int i, int plot) {
    if (EditMapInfo == nullptr || EditMapInfo->work.events.points == nullptr || i < 0 || i >= EditMapInfo->event_count) {
        return;
    }
    const ED_EVENT_POINT &p = EditMapInfo->work.events.points[i];

    if (p.event_type == 1 && Chara != nullptr && (plot < 0 || p.parts_no == plot)) {
        ED_EVENT_POINT one = p;
        ED_EVENT_PARAM param;
        sceVu0FVECTOR  from = {};

        Chara->GetPosition(from);
        one.trigger_range[0] = 1.0e9f;
        one.completion_flag = 0;
        bool found = false;

        // (a door is found only by one facing its way, within 0.8 of a turn's radians: every facing round, every height a house may stand at)
        for (int turn = 0; turn < 8 && !found; turn++) {
            const sceVu0FVECTOR facing = {0.0f, -3.14159265f + turn * 0.785398f, 0.0f, 0.0f};

            for (int dy = -1000; dy <= 1000 && !found; dy += 10) {
                sceVu0FVECTOR level = {from[0], from[1] + dy, from[2], 1.0f};

                found = EdGetEvent(&one, 1, &param, level, const_cast<float *>(facing), NowTime) != 0;
            }
        }
        if (found) {
            std::fprintf(stderr, "coop-town: test: map %d point %d door to '%.16s' in the world at (%.1f %.1f %.1f) facing %.2f\n", MapNo, i, p.destination,
                         param.position[0], param.position[1], param.position[2], param.rotation[1]);
        } else {
            std::fprintf(stderr, "coop-town: test: map %d point %d door to '%.16s': not found by the game's search from (%.0f %.0f %.0f) (part %d %s, frame %s, progress %d/%d, hours %.2f-%.2f, now %.2f)\n",
                         MapNo, i, p.destination, from[0], from[1], from[2], p.parts_no,
                         p.parts_no >= 0 && EdExchangeInfo.ground != nullptr && EdExchangeInfo.ground->GetPartsObject(p.parts_no) != nullptr ? "placed" : "not placed",
                         p.frame == nullptr ? "none" : (p.frame->attr.draw_on & 1) != 0 ? "drawn" : "hidden", p.minimum_progress, p.secondary_progress, p.start_time,
                         p.end_time, NowTime);
        }
    }
}

void CoopTownPlotDoorsForTest(int plot) {
    for (int i = 0; EditMapInfo != nullptr && i < EditMapInfo->event_count; i++) {
        CoopTownDoorWorldForTest(i, plot);
    }
}

// Test hook (DC_COOP_TEST_EVENT_POINTS=1; coop-pt1106-batch): the town's map event points once, on its first walk step: each
// one's slot, kind, script event, place, reach and flags, and for a map event the script's ext calls ("ext:count") and
// whether either player may start it as their own (CoopWorldsPartnerRoomEvent: no story, nobody taken anywhere).
static void EventPointsForTest() {
    static const bool on = TestEnvOn("DC_COOP_TEST_EVENT_POINTS");
    static int        said_map = -1;

    if (!on || EditMapInfo == nullptr || EdInteriorFlag != 0 || said_map == MapNo) {
        return;
    }
    // (coop-soakfix3: a row's fixture first, when it has one: before its load the town's parts have no frames for their doors yet)
    if (TestEnv("DC_STATE_LOAD_AT") != nullptr && !NetplayStateFileLoaded()) {
        return;
    }
    said_map = MapNo;

    const ED_EVENT_POINT *points = EditMapInfo->work.events.points;

    for (int i = 0; points != nullptr && i < EditMapInfo->event_count; i++) {
        const ED_EVENT_POINT &p = points[i];
        std::string           exts;

        if (p.event_type == ED_EVENT_POINT_EVENT && EdEventData != nullptr && p.side > 0) {
            for (int ext = 0; ext < 1200; ext++) {
                if (const int n = ScriptExtCalls(reinterpret_cast<const RS_PROG_HEADER *>(EdEventData), p.side, ext, nullptr, 0); n > 0) {
                    exts += " " + std::to_string(ext) + ":" + std::to_string(n);
                }
            }
        }
        std::fprintf(stderr, "coop-town: test: map %d point %d kind %d on %d event %d at (%.0f %.0f %.0f) reach %.0f parts %d flag %d dest '%.16s'%s%s\n", MapNo, i,
                     p.event_type, p.enabled, p.side, p.position[0], p.position[1], p.position[2], p.trigger_range[0], p.parts_no, p.completion_flag,
                     p.destination, exts.empty() ? "" : " exts", exts.c_str());
        if (p.event_type == ED_EVENT_POINT_EVENT && p.side > 0) {
            std::fprintf(stderr, "coop-town: test: map %d point %d event %d either player's own: %s\n", MapNo, i, p.side,
                         CoopWorldsPartnerRoomEvent(EdEventData, p.side) ? "yes" : "no");
        }
        CoopTownDoorWorldForTest(i, -1);
    }
}

void CoopTownStep() {
    coop::BagSizeStep(); // coop-bags
    EventPointsForTest(); // coop-pt1106-batch
    AtMapEventForTest();  // coop-pt1106-batch
    HerHeldCrossTrace(); // coop-menucross
    coop::StepTownNotes(); // coop-menucross
    DoorNotesStep();       // coop-cavehold
    CaveActsStep(); // coop-doorfix: a player's refusal or walk in at the cave door, beside the game
    CaveDoorSquares(); // coop-doorprompt
    CoopTownDoorFadeSaid(); // coop-doorfade
    // coop-localfix: DC_COOP_DOORTRACE=1: Toan's walk at a door, each tick it is in reach (event ready): its kind, his Cross
    // as the walk reads it, the key lock and her own menu (Jep's bar: his door must open whatever she has open).
    if (static const bool trace = TestEnvOn("DC_COOP_DOORTRACE"); trace && EdMoveCharaInfo.event_ready != 0) {
        InputPadState one{};
        const bool    linked = NetplayPad(0, one);

        std::fprintf(stderr, "coop-town: door trace: kind %d cross %d (pad down %d on %d, link pad 1 %04x %d) key_lock %d view %d her menu %d\n",
                     (int) EdMoveCharaInfo.param.kind, EdPadDown(0x40, 1), GamePad.Down(0x40), GamePad.On(0x40), one.buttons, (int) linked,
                     (int) key_lock, EdCheckViewMode(), (int) CoopP2TownMenuOn());
    }
    XiaoAlwaysJoined(); // coop-p2skin

    // coop-camjump: after the game's walk step that follows the split (this copy's own walk runs the split, then this), how far
    // that step took the eye her screen was handed, and the circle it left (w-town-apart -SplitCam decides on this line).
    if (g_split_cam_check > 0 && ++g_split_cam_check > 2) {
        sceVu0FVECTOR now;

        g_split_cam_check = 0;
        MainCamera.GetPos(now);
        std::fprintf(stderr, "coop-town: the game camera's first step after the split moved her eye %.1f (distance %.1f height %.1f)\n",
                     DistVector(now, g_split_cam_eye), MainCamera.distance, MainCamera.height);
    }

    // coop-localfix (Jep's 2026-10-26 run: "leaving the dungeon made P2s game freeze"): gameTask is the dungeon loop's, and a
    // dungeon left by its way out leaves it at EXIT_FADE_WAIT (176) in the town, where everything that waits for "play" (the
    // merge's snapshot take, the checkpoints, the menus) waited for good. The town's walk puts it back to play on every copy
    // alike (each town tick). DC_NETPLAY_MUTATE=stale_task_kept (twin): left as the dungeon left it.
    if (gameTask >= GAME_TASK_ESCAPE_INIT && gameTask <= GAME_TASK_EXIT_FADE_WAIT && !NetplayMutated("stale_task_kept")) {
        gameTask = GAME_TASK_PLAY;
    }

    g_indoor.on = false; // coop-inbuild

    // coop-machotalk: her own talk outdoors stands her (EdTalkMode turns her to the villager and steps her, CoopTownHerTalkStepIn),
    // shown where it has her, as in a room (CoopTownInteriorStep).
    if (g_room_event.on && !CoopWorldsApart() && g_town.body != nullptr) {
        g_town.body->GetPosition(g_town.pos);
        g_town.pos[3] = 1.0f;
        g_town.visible = true;
        return;
    }
    TownBoxWarpForTest(); // coop-townchest: test hook
    PartnerTownBox();     // coop-townchest: her Cross at a town chest
    PartnerStep();
}

// coop-inbuild: her Cross at a door of the room (linked and together, the game's own test of a door from where she stands
// and the way she faces): she goes out by it, and the copies split there unless Toan is beside her. True when this
// copy goes out now. DC_COOP_MUTATE=no_indoor_door (twin): her Cross opens nothing indoors, as before.
// coop-pt1106-batch: her door out of the room opening together (CoopWorldsDoorWindowStart): on both copies she stands at the door
// as Toan does at his, its open motion (where her body has it: the walk's, else standing), its sound, the room's fade (out of
// sight on his copy); as it ends (Toan's own count, 140 steps) the copies split: hers goes out by the door, his stays.
struct HerDoor {
    bool          on = false;
    int           door = -1;
    int           count = 0;
    int           sound = -1;
    sceVu0FVECTOR pos = {};
    sceVu0FVECTOR rot = {};
};

HerDoor g_her_door;

static bool HerDoorStep() {
    CCharacter *body = g_town.body;

    if (body != nullptr && body->frame != nullptr) {
        body->SetVelocity(CVector3_f_(0.0f, 0.0f, 0.0f));
        body->Step();
        body->ShadowStep();
        body->SetPosition(g_her_door.pos);
        body->frame->SetPosition(g_her_door.pos);
        body->SetRotation(g_her_door.rot[0], g_her_door.rot[1], g_her_door.rot[2]);
        body->frame->SetRotation(g_her_door.rot[0], g_her_door.rot[1], g_her_door.rot[2]);
        body->ClothStep(0);
        sceVu0CopyVector(g_town.pos, g_her_door.pos);
        g_town.pos[3] = 1.0f;
        g_town.visible = true;
    }

    if (--g_her_door.count == 100 && g_her_door.sound >= 0) {
        EdDoorOpenSe(g_her_door.sound, g_her_door.pos);
    }
    if (g_her_door.count >= 0 && CoopWorldsDoorWindowBy() == 2) {
        return false;
    }

    const int door = g_her_door.door;

    g_her_door.on = false;
    CoopWorldsDoorWindowEnd();

    if (!CoopWorldsLeaveBuildingBy(2)) {
        EdFadeIn(1, 0.0f, 0.0f, 0.0f); // (his copy stays: the door's fade was hers)
        return false;
    }

    // Her copy: its player takes her place (her camera too, as at his door's split) and goes out by the door, its fade done.
    float at[3] = {};

    TownPlacePlayerAtPartner(at);
    EdInLeaveByDoor(door);
    std::fprintf(stderr, "coop-town: player 2 goes out by the room's door %d after it opened together (link tick %lld)\n", door, (long long) NetplayLinkTick());
    return true;
}

static bool IndoorPartnerDoor() {
    static bool   cross_was = false;
    InputPadState pad;

    const bool linked = NetplayPad(1, pad);
    const bool cross = linked && (pad.buttons & kInputCross) != 0;
    const bool pressed = cross && !cross_was;

    cross_was = cross;

    if (!pressed || !g_town.visible || !g_indoor.was || g_town.body == nullptr || !CoopWorldsPartnerMayStart(nullptr, -1) || Mutated("no_indoor_door")) {
        return false;
    }

    sceVu0FVECTOR rotation;

    g_town.body->frame->GetRotation(rotation);

    const int door = EdInDoorAt(g_town.pos, rotation);

    if (CoopDebug()) {
        std::fprintf(stderr, "worlds: player 2's Cross indoors at (%.0f %.0f %.0f) facing %.2f: door %d\n", g_town.pos[0], g_town.pos[1], g_town.pos[2],
                     rotation[1], door);
    }

    if (door < 0 || CoopWorldsDoorWindowBy() != 0) { // (coop-pt1106-batch: not while a door opens together)
        return false;
    }

    // coop-pt1106-batch: together, her door opens on both copies first, the split as it ends (HerDoorStep).
    // (coop-reds1109: DC_COOP_MUTATE=herdoor_jumps, w-indoor-inleave-doorfade~twin, skips this door too: out at once, no door, as
    // before coop-housefade; it reached only the apart path below, which no longer runs while both are inside)
    if (!Mutated("herdoor_jumps") && CoopWorldsDoorWindowStart(2)) {
        int motion = EdInDoorOpenPose(door, g_her_door.pos, g_her_door.rot, &g_her_door.sound);

        g_her_door.on = true;
        g_her_door.door = door;
        g_her_door.count = 140; // (Toan's door's own count, EdIn_door_open_cnt)
        g_her_door.pos[3] = 1.0f;
        if (g_town.body != nullptr) {
            g_town.body->SetMotion(motion <= 2 ? motion : 0, 6); // (her body has the walk's motions only, coop-xiaopose)
        }
        EdFadeOut(100, 0.0f, 0.0f, 0.0f);
        std::fprintf(stderr, "coop-town: player 2 opens the room's door %d together (link tick %lld): Toan's motion %d, she at (%.0f %.0f %.0f)\n", door,
                     (long long) NetplayLinkTick(), motion, g_her_door.pos[0], g_her_door.pos[1], g_her_door.pos[2]);
        return false;
    }

    if (!CoopWorldsLeaveBuildingBy(2)) {
        return false;
    }

    // coop-housefade (Jep's 2026-11-02 playtest: "if p1 and p2 are both in the house, p2 won't play the door opening animation
    // when leaving"): her copy, split from his here, went out on this tick with no door at all (no open, no fade, no sound).
    // Its player takes her place and her camera (as at his door's split), then opens the door as Toan's opens: the door's
    // place and facing, its open motion, the fade, its sound (EdInOpenDoorBy); the room's leave mode takes her out. Both going
    // out together (Toan beside her) leaves at once, as before. DC_COOP_MUTATE=herdoor_jumps (twin): out at once, as before.
    if (CoopWorldsApart() && NetplayRole() == 2 && !Mutated("herdoor_jumps")) {
        float at[3] = {};

        TownPlacePlayerAtPartner(at);
        EdInOpenDoorBy(door);
        std::fprintf(stderr, "coop-town: player 2 opens the room's door %d (link tick %lld): her copy at (%.0f %.0f %.0f), its fade out\n", door,
                     (long long) NetplayLinkTick(), at[0], at[1], at[2]);
        return false;
    }

    EdInLeaveByDoor(door);
    return true;
}

namespace {
}

namespace dc::game {
int EdInSceneEvent(); // edit_in.cpp (coop-scenejoin)
} // namespace dc::game

// coop-scenejoin (Jep, 2026-10-27: "if P2 joins an area where P1 is in a cutscene, P2 must view that cutscene"): together in a
// room whose story scene runs (its arrival, the mayor's first visit: CoopWorldsSceneEvent), as after her merge into it. Read
// from the room's own statics and mode, the same on both copies. DC_WORLDS_MUTATE=scene_view_own (twin): merged into it, but
// her own camera and her drawn, as before. (Here, not in CoopWorldsSceneEvent: there it also hid the scene's start line the
// twin's mode check reads, so it never ran: coop-twinfix.)
static bool SceneRuns() {
    return EdInteriorFlag != 0 && g_indoor.mode == 4 && EdInSceneEvent() > 0 && !CoopWorldsApart(); // ED_IN_MODE_EVENT
}

static bool SceneHolds() {
    return SceneRuns() && !CoopWorldsMutated("scene_view_own");
}

// coop-pt1109e: a story scene both copies are watching together, in a room or in town (not her own room event, which he walks on beside).
bool CoopTownSceneWatchedTogether() {
    if (!NetplayHasPlayer() || CoopWorldsApart() || g_room_event.on) {
        return false;
    }
    return EdInteriorFlag != 0 ? g_indoor.mode == 4 /* ED_IN_MODE_EVENT */ : ::GameMode == ED_MODE_EVENT;
}

// Each room step: the scene's start and end on this copy, with how many steps she was held through it (the tests read it).
// coop-mayorhide test hook (DC_COOP_TEST_ROOM_PEOPLE=1, w-indoor -Case mayorstays): together in a room, every 30 link ticks, the
// room's people drawn (the mayor, i01h06) and whether her room event runs, on both copies.
static void RoomPeopleTestLine(int mode) {
    static const bool on = TestEnvOn("DC_COOP_TEST_ROOM_PEOPLE");

    if (!on) {
        return;
    }
    if (!g_indoor.was || CoopWorldsApart() || NetplayLinkTick() % 30 != 0) {
        return;
    }

    int drawn = 0;

    for (const CNPCharacter &villager : EdVillager) {
        drawn += villager.initialized != 0 && villager.draw_enabled != 0 ? 1 : 0;
    }
    std::fprintf(stderr, "coop-mayorhide: test: p%d link tick %lld room mode %d her event %d villagers drawn %d\n", NetplayLocalPlayer(), (long long) NetplayLinkTick(), mode,
                 g_room_event.on ? g_room_event.event : -1, drawn);
    std::fflush(stderr);
}

static void SceneHoldLog() {
    static int held = 0;

    if (SceneRuns()) { // (the scene's start and end lines even under scene_view_own: its twin's checks read them)
        if (held++ == 0 && SceneHolds()) {
            std::fprintf(stderr, "coop-town: story scene %d together in %s: she is held and hidden, player 2's screen shows it from the game's camera (link tick %lld), she stands at (%.1f %.1f %.1f)\n",
                         EdInSceneEvent(), EdInteriorName, (long long) NetplayLinkTick(), g_town.pos[0], g_town.pos[1], g_town.pos[2]);
        }
    } else if (held > 0) {
        std::fprintf(stderr, "coop-town: story scene over in %s after %d steps together (link tick %lld), she stands at (%.1f %.1f %.1f)\n", EdInteriorName, held,
                     (long long) NetplayLinkTick(), g_town.pos[0], g_town.pos[1], g_town.pos[2]);
        held = 0;
    }
}

bool CoopTownInteriorStep(CMapParts *parts, int parts_count, CCameraFollow *room, bool markers, int mode, bool steps) {
    g_indoor.on = true;
    g_indoor.parts = parts;
    g_indoor.parts_count = parts_count;
    g_indoor.room = room;
    g_indoor.markers = markers;
    g_indoor.mode = mode;
    if (mode == 0) { // ED_IN_MODE_WALK
        g_indoor.walked = NetplayLinkTick();
    }

    // (opt8: his talk from its slot is his talk for the test hooks and their lines, the room walking)
    const int talk_mode = mode == 0 && g_talks[0].on ? 3 : mode; // ED_IN_MODE_TALK

    RoomTalkForTest(talk_mode); // coop-mayorhouse
    TalkDoorForTest(); // coop-pt1109f
    HisTalkBesideHers(talk_mode); // coop-talks2
    TalkHiddenSaid(); // opt8
    SceneHoldLog(); // coop-scenejoin
    RoomPeopleTestLine(mode); // coop-mayorhide

    // coop-padswap test hook (DC_COOP_TEST_PAD_PROBE): where Toan and she stand as each phase of the pad probe starts and ends.
    if (static int probe_was = 0; NetplayPadProbePhase() != probe_was && Chara != nullptr) {
        sceVu0FVECTOR toan;

        Chara->GetPosition(toan);
        std::fprintf(stderr, "coop: test: pad probe phase %d at link tick %lld (room mode %d, pad %s's): Toan (%.1f %.1f) her (%.1f %.1f)\n",
                     NetplayPadProbePhase(), (long long) NetplayLinkTick(), mode, CoopWorldsP2Drives() ? "player 2" : "player 1", toan[0], toan[2],
                     g_town.pos[0], g_town.pos[2]);
        probe_was = NetplayPadProbePhase();
    }
    RoomEventTestShots(); // coop-roomevent
    RoomItemTestLine(); // coop-closetitem

    // DC_COOP_MUTATE=indoor_event_holds (twin): she stands through Toan's events and talks too.
    if (Mutated("indoor_event_holds") && (mode == 3 || mode == 4)) {
        steps = false;
    }

    // Apart, player 2's copy has her skin on Toan (coop-p2skin's CoopTownInteriorStep, before this) and no partner.
    // coop-houseenter: but her own merge's (she was in the room first): she is the partner from its start, on both copies, as
    // in town (PartnerStep).
    if (CoopWorldsApart() && !NetplayStayerMerge()) {
        g_indoor.was = true;

        // coop-pt1109f: the pair split in a room (his door): her talk goes on on her copy, as this copy's own; his has left.
        HerTalkAtTheSplit(mode == 0); // ED_IN_MODE_WALK

        // DC_COOP_DEBUG: every 60th step, the room's mode and where her copy's player stands (the p2scene test reads it).
        if (static int apart_steps = 0; CoopDebug() && NetplayRole() == 2 && Chara != nullptr && ++apart_steps % 60 == 1) {
            sceVu0FVECTOR pos;
            Chara->GetPosition(pos);
            int villagers = 0; // coop-mayorhouse: the room's people drawn (the mayor, i01h06)

            for (const CNPCharacter &villager : EdVillager) {
                villagers += villager.initialized != 0 && villager.draw_enabled != 0 ? 1 : 0;
            }
            std::fprintf(stderr, "coop-town: player 2 apart in %s: room mode %d, at (%.1f %.1f %.1f), step %d, villagers drawn %d\n", EdInteriorName, mode, pos[0],
                         pos[1], pos[2], apart_steps, villagers);
        }

        ApartDoorForTest(mode);
        ApartEventForTest(mode); // coop-closet

        // coop-housefade (Jep's 2026-11-02 playtest: "when P1 leaves a house, P2 vanishes from P1's screen right away instead
        // of staying until the fade-out"): his Cross on the room's door splits the pair on its tick, and apart this step
        // showed nobody (the skin step before it puts her away), so his copy drew her none of the door's frames. As
        // coop-doorfade does through a town door: player 1's copy keeps her standing where she was, drawn, while his door
        // opens and fades (the town's load puts her away). DC_COOP_MUTATE=roomdoor_hides (twin): put away at once, as before.
        if (mode == 1 && g_room_shown_before && NetplayRole() != 2 && g_town.body != nullptr && g_town.body->frame != nullptr && // ED_IN_MODE_DOOR_OPEN
            !Mutated("roomdoor_hides")) {
            StandThroughDoorFade();
        }

        // And her own door out of the room (her copy opens it and fades, IndoorPartnerDoor): player 1's copy keeps her standing
        // where she was, drawn, for as long as that fade runs on hers (kHerLeaveMs), as coop-dunentry does in town.
        // DC_COOP_MUTATE=herdoor_hides (twin): put away at once, as before.
        static int her_door_drawn = 0;

        if (const long long left_ms = CoopWorldsHerLeaveMs(); left_ms >= 0 && !Mutated("herdoor_hides")) {
            if (left_ms < kHerLeaveMs && (g_room_shown_before || her_door_drawn > 0) && g_town.body != nullptr && g_town.body->frame != nullptr) {
                if (her_door_drawn++ == 0) {
                    std::printf("coop: her door out of the room apart: player 2 stands at (%.0f %.0f %.0f) through her fade\n", g_town.pos[0], g_town.pos[1],
                                g_town.pos[2]);
                }
                StandThroughDoorFade();
            } else if (her_door_drawn > 0) {
                std::fprintf(stderr, "coop: her door out of the room apart: player 2 drawn %d frames on player 1's copy after she left\n", her_door_drawn);
                her_door_drawn = -1; // said; until her next way out
            }
        } else if (left_ms < 0) {
            her_door_drawn = 0;
        }
        return false;
    }

    // (coop-fx51c: from a state file's load on, when a row loads one: before it her copy plays as the fixture's maker did, so
    // the load finds the saved game and the twin's own check fails, not its fixture's checksum)
    if (Mutated("no_indoor_partner") && !NetplayStateLoadPending()) {
        g_town.visible = false;
        g_indoor.was = false;
        return false;
    }

    // coop-scenejoin: his story scene in the room (she came in during it, or was here as it started): she stands through it
    // and neither screen draws her (SceneHolds); her screen shows it from the game's camera (OwnTownView).
    if (SceneHolds()) {
        steps = false;
    }

    // coop-pt1106-batch: her door opening together: she stands at it, then the split.
    if (g_her_door.on) {
        return HerDoorStep();
    }

    IndoorHisClosetForTest(); // coop-pt1108b

    // Toan's event, door or leaving: she stands where she is, as outside in his events; not placed in this room yet, not shown.
    if (!steps) {
        if (!g_indoor.was) {
            g_town.visible = false;
        }

        return false;
    }

    // coop-roomevent: her own room event moves her (her body is its main character): her walk stands, she is shown where it
    // has her. opt8: her talk too, as retail's room talk mode runs no MoveCharacter: her walk and her room camera's step (IndoorCamStep:
    // marker, follow, stick) hold, so her stick turns nothing (w-indoor -FollowCam's check). DC_COOP_MUTATE=talk_partner_walks (twin): her
    // walk and camera step run through her talk.
    if (g_room_event.on && !(g_talks[1].on && Mutated("talk_partner_walks"))) {
        g_town.body->GetPosition(g_town.pos);
        g_town.pos[3] = 1.0f;
        g_town.visible = true;
        return false;
    }

    PartnerStep();
    IndoorDoorForTest();
    IndoorEventForTest(); // coop-p2collide
    IndoorTalkSpotForTest(); // coop-hertalk
    return IndoorPartnerDoor();
}

bool CoopTownIndoorPose(float *pos) {
    if (!g_town.visible || !g_indoor.was || EdInteriorFlag == 0) {
        return false;
    }

    std::copy(g_town.pos, g_town.pos + 3, pos);
    return true;
}

bool CoopTownIndoorWalks() {
    return EdInteriorFlag != 0 && g_indoor.mode == 0; // ED_IN_MODE_WALK
}

// coop-stage4 4c: the room's menu went on as a local one: its walk from now on, not from the room's next step (the take may come this tick).
void CoopTownIndoorMenuLeft() {
    g_indoor.mode = 0; // ED_IN_MODE_WALK
}

bool CoopTownIndoorP1Menu() {
    return EdInteriorFlag != 0 && (g_indoor.mode == 6 || g_indoor.mode == 7); // ED_IN_MODE_MENU_INIT, ED_IN_MODE_MENU
}

int CoopTownIndoorMode() {
    return EdInteriorFlag != 0 ? g_indoor.mode : -1;
}

// coop-mayorhouse (Jep's 2026-10-27 playtest: "when P1 talks to the mayor while P2 is in the house, the mayor's chat bubble
// appears on P2's screen"): together in a room, a talk is his (talks stay Toan's), and both copies step it, so his window
// was drawn on hers too. It steps as it draws (its fade, its bubble), so her copy still draws it, into a target nobody sees.
// DC_COOP_MUTATE=his_talk_shown (twin): drawn on her screen, as before.
namespace {
gfx::TextureHandle g_talk_hidden = gfx::kNullTexture;
gfx::TextureHandle g_talk_previous = gfx::kNullTexture;
int                g_talk_hidden_frames = 0;
bool               g_talk_rect_said = false;

} // namespace

void CoopTownHisTalkHiddenEnd(bool hidden) {
    if (hidden) {
        gfx::SetRenderTarget(g_talk_previous);
    }
}

// opt8: his talk outdoors together, from his slot (or on the town's talk mode under the talk_town_mode twin): the tests' lines.
static bool HisTownTalkRuns() {
    return EdInteriorFlag == 0 && Linked() && !CoopWorldsApart() && (g_talks[0].on || (::GameMode == ED_MODE_TALK && !CoopTownSwapDeferred()));
}

// coop-pt1106-batch (Jep's 2026-11-06 playtest, item 3: P2 could use Norune's fishing board only with P1 out of the area, and
// "when P1 reads the sign, the text box shows on both screens"): outdoors, linked and together, a town event either player may
// start as their own (one of the town's map event points, CoopWorldsPartnerRoomEvent: no story, nobody taken anywhere) is its
// reader's alone, as a room's pickup is (coop-roomevent): the town's one script engine runs it, its camera and windows on the
// reader's screen; the other player walks on with their own camera, its windows drawn out of sight on their copy. Hers: her
// Cross starts it (HerTownEventAt), her body its main character, her pad its pad, Toan walking on beside it with his camera
// and nothing of his starting till it ends. His: as retail on his copy, she walks on beside it. Story events stay together,
// both screens on them (coop-exitscene). DC_COOP_MUTATE=town_event_shared (twin): his runs together as before, both screens
// on it, she stands; town_event_shown (twin): his windows on her screen too; roomevent_shown (twin): her event's camera and
// windows on his screen too.
namespace {
int          g_own_event_reader = 0; // whose the running town event is (1, 2), 0 none (a shared one)
int          g_own_event_no = 0;
std::int64_t g_own_event_tick = 0;
float        g_own_event_clock = 0.0f; // the clock as it began
int          g_own_event_hidden = 0; // its windows' frames drawn out of sight on the other copy
float        g_own_event_from[3] = {}; // where the other player stood as it began
float        g_own_event_far = 0.0f;   // and the furthest they walked from there through it

bool TownMapEvent(int event_no) {
    const ED_EVENT_POINT *points = EditMapInfo != nullptr ? EditMapInfo->work.events.points : nullptr;

    for (int i = 0; points != nullptr && i < EditMapInfo->event_count; i++) {
        if (points[i].event_type == ED_EVENT_POINT_EVENT && points[i].side == event_no) {
            return true;
        }
    }
    return false;
}

void OtherPlayerAt(int reader, float *at) {
    if (reader == 2 && Chara != nullptr) {
        sceVu0FVECTOR toan;

        Chara->GetPosition(toan);
        std::copy(toan, toan + 3, at);
    } else {
        std::copy(g_town.pos, g_town.pos + 3, at);
    }
}
} // namespace

// coop-pt1106-batch (both by Norune's fishing board): Toan's walk queues a map event he stands at each tick (EdMoveChara), and its
// script then waits for his Cross on his pad; so her Cross with him in reach found the town never free and read nothing, and
// nothing ran. A queued one of those (story-free: either player's own) is hers to take with her Cross, unless his own Cross
// is down this tick (both at once: his, as his walk asked first). DC_COOP_MUTATE=queued_event_holds (twin): his queue holds
// the town, as before.
bool CoopTownMapEventQueued(int event_no) {
    if (EdInteriorFlag != 0 || !Linked() || CoopWorldsApart() || EditMapInfo == nullptr || !TownMapEvent(event_no) ||
        !CoopWorldsPartnerRoomEvent(EdEventData, event_no) || Mutated("queued_event_holds")) {
        return false;
    }

    const PAD_DATA *data = reinterpret_cast<const PAD_DATA *>(&GamePad); // pad[0], pad[1], previous_pad[0], previous_pad[1]
    const bool      toan_cross = (data[0].input.status.button & kInputCross) != 0 && (data[2].input.status.button & kInputCross) == 0;

    if (toan_cross) {
        static std::int64_t said = -1;

        if (said != NetplayLinkTick()) {
            said = NetplayLinkTick();
            std::fprintf(stderr, "coop-town: his Cross at the town's event %d this tick (link tick %lld): his\n", event_no, (long long) NetplayLinkTick());
        }
        return false;
    }
    return true;
}

// coop-pt1108d (Jep's 11-08 playtest: P1 before the mayor's closet, its prompt over his head: P2 could not talk to the mayor): as
// the town's above, in a room. EdMoveChara queues the event point he stands at each step, Cross or not, and its script waits for his
// Cross, so edit_in never found the room free and her Cross read nothing. Any event point: her talk is her own whatever the script
// (a pickup of hers is checked by HerRoomPickupAt). His own Cross this tick keeps it his. DC_COOP_MUTATE=room_queue_holds (twin): holds, as before.
bool CoopTownRoomEventQueued(int event_no) {
    if (EdInteriorFlag == 0 || !Linked() || CoopWorldsApart() || EdInInfo == nullptr || Mutated("room_queue_holds")) {
        return false;
    }

    bool found = false;

    for (int i = 0; i < 32 && !found; i++) {
        found = EdInInfo->event_points[i].event_type == ED_EVENT_POINT_EVENT && EdInInfo->event_points[i].side == event_no;
    }

    if (static int said = -1; found && said != event_no) {
        said = event_no;
        std::fprintf(stderr, "coop-town: his walk queues the room's event %d (link tick %lld)\n", event_no, (long long) NetplayLinkTick());
    }

    const PAD_DATA *data = reinterpret_cast<const PAD_DATA *>(&GamePad); // pad[0], pad[1], previous_pad[0], previous_pad[1]

    return found && !((data[0].input.status.button & kInputCross) != 0 && (data[2].input.status.button & kInputCross) == 0);
}

void CoopTownTownEventStarts(int event_no) {
    g_own_event_reader = 0;
    if (EdInteriorFlag != 0 || !Linked() || CoopWorldsApart() || EditMapInfo == nullptr || event_no <= 0 || !TownMapEvent(event_no) ||
        !CoopWorldsPartnerRoomEvent(EdEventData, event_no)) {
        return;
    }

    const bool hers = g_room_event.on && !g_talks[1].on && g_room_event.event == event_no;

    if (!hers && Mutated("town_event_shared")) {
        return;
    }
    g_own_event_reader = hers ? 2 : 1;
    g_own_event_no = event_no;
    g_own_event_tick = NetplayLinkTick();
    g_own_event_hidden = 0;
    g_own_event_far = 0.0f;
    OtherPlayerAt(g_own_event_reader, g_own_event_from);
    g_own_event_clock = SaveData != nullptr ? SaveData->GetNowTime() : 0.0f;
    std::fprintf(stderr, "coop-town: the town's event %d is player %d's own at link tick %lld (the other walks on, its windows out of sight on their copy)\n",
                 event_no, g_own_event_reader, (long long) NetplayLinkTick());
    std::fflush(stderr);
}

int CoopTownOwnTownEvent() {
    return ::GameMode == ED_MODE_EVENT && EdInteriorFlag == 0 ? g_own_event_reader : 0;
}

// Each town tick: how far the other player walked through it, and its end said (the tests read both).
static void OwnTownEventTick() {
    if (g_own_event_reader == 0) {
        return;
    }
    if (::GameMode == ED_MODE_EVENT && EdInteriorFlag == 0 && Linked() && !CoopWorldsApart()) {
        float at[3];


        OtherPlayerAt(g_own_event_reader, at);
        g_own_event_far = std::max(g_own_event_far, std::hypot(at[0] - g_own_event_from[0], at[2] - g_own_event_from[2]));
        return;
    }
    std::fprintf(stderr, "coop-town: player %d's own town event %d over at link tick %lld (%lld ticks): the other walked %.1f through it, its windows %d frames out of sight on player %d's copy\n",
                 g_own_event_reader, g_own_event_no, (long long) NetplayLinkTick(), (long long) (NetplayLinkTick() - g_own_event_tick), g_own_event_far,
                 g_own_event_hidden, 3 - g_own_event_reader);
    std::fflush(stderr);
    std::fprintf(stderr, "coop-town: the clock moved %.3f through that event\n", SaveData != nullptr ? SaveData->GetNowTime() - g_own_event_clock : 0.0f); // (x-town-sign's line)
    g_own_event_reader = 0;
}

// Through his own town event she walks on (editloop's event mode steps her as its walk does); through hers he does.
// DC_COOP_MUTATE=town_event_shared (twin): she stands through his, as before.
bool CoopTownWalksThroughOwnEvent(int walker) {
    return CoopTownOwnTownEvent() == 3 - walker;
}

// Toan's walk beside her event: his Cross starts nothing (the town has one script engine: no door, talk or event of his till hers
// ends; editloop also puts back any event his walk asked for). Around MainMode.
static std::uint32_t g_his_cross_was = 0;

void CoopTownHisWalkInHerEventIn() {
    PAD_DATA *data = reinterpret_cast<PAD_DATA *>(&GamePad); // pad[0], pad[1], previous_pad[0], previous_pad[1]

    g_his_cross_was = data[0].input.status.button & kInputCross;
    data[0].input.status.button &= ~kInputCross;
}

void CoopTownHisWalkInHerEventOut() {
    PAD_DATA *data = reinterpret_cast<PAD_DATA *>(&GamePad);

    data[0].input.status.button |= g_his_cross_was;
}

// Its windows, sprites and fade on the other player's copy: drawn out of sight (they step as they draw).
bool CoopTownOwnEventHiddenBegin(bool windows) {
    const int reader = CoopTownOwnTownEvent();
    const int local = NetplayLocalPlayer();

    if (reader == 0 || local == 0 || local == reader || Mutated(reader == 2 ? "roomevent_shown" : "town_event_shown")) {
        return false;
    }
    if (g_talk_hidden == gfx::kNullTexture) {
        g_talk_hidden = gfx::CreateRenderTarget(640, 480, true);
    }
    if (windows) {
        g_own_event_hidden++;
    }
    g_talk_previous = gfx::CurrentRenderTarget();
    gfx::SetRenderTarget(g_talk_hidden);
    return true;
}

// coop-pt1108f (Jep's 11-09 playtest: the clock gone from his screen through her sign): the running own town event is another player's, so this copy's
// player walks on through it and sees the town's clock (retail hides it in event mode: one player). (coop-scriptmenus: its script's menus no longer
// run on this copy at all, coop_scriptmenu.cpp; the old 'out of sight' twin scriptmenu_shown went with them.)
bool CoopTownOwnEventIsOthers() {
    const int reader = CoopTownOwnTownEvent();
    const int local = NetplayLocalPlayer();

    return reader != 0 && local != 0 && local != reader;
}

// coop-scriptmenus: the player whose pad the running event reads (its script's menus are theirs): a room or town event of a
// player's own reads that player's pad (CoopTownHerRoomEventStepIn swaps it in; coop-small2: the player it carries, never a fixed
// 2), anything else the game's pad, whoever's the routing gives it (player 1's, or the starter's of a scene she owns:
// NetplayGamePadPlayer). DC_COOP_MUTATE=scriptmenu_owner_gamepad (twin): the game pad's player always, the room event's player
// not read.
int CoopTownEventPadPlayer() {
    if (g_room_event.pad_in && g_room_event.player > 0 && !Mutated("scriptmenu_owner_gamepad")) {
        return g_room_event.player;
    }
    return NetplayGamePadPlayer();
}

// Each town tick (editloop, before her talk's start and step): his talk beside hers said (the tests read it), the time her copy
// hid his window said as it ends, her talk ended if the pair split. Test hook DC_COOP_TEST_TOWN_TALK=K: linked and together
// in the town's walk, from link tick K, the first villager drawn: Toan is put 9 before it (a little round to one side) facing
// it, and 30 link ticks into the talk his Cross then starts she is put 9 before it (round to the other side), on both copies
// alike, so her Cross talks to it beside his talk (Jep's playtest: both at Macho).
// coop-stage4 r6 test hook (DC_COOP_TEST_TOWN_TALK_APART=N), the town's twin of DC_COOP_TEST_ROOM_TALK: on player 1's copy apart
// outdoors, at his Nth walk tick, Toan is put before the town's first villager set up (drawn for the test, as DC_COOP_TEST_TOWN_TALK),
// facing it, so his next Cross talks to it; once that talk is over, turned away (his later Crosses talk to nobody). x-town-stayermenu
// -Talk -Outdoors: her merge into his outdoor talk.
static void TownTalkApartForTest() {
    static const int at = TestEnvInt("DC_COOP_TEST_TOWN_TALK_APART", -1);
    static const int role = TestEnvInt("DC_COOP_TEST_TOWN_TALK_ROLE", 1); // (coop-stage4 4e part 2: =2 her copy, the game's player her skin)
    static int       steps = 0, phase = 0, villager_no = -1; // phase: 0 waits, 1 placed, 2 talking, 3 turned away

    if (at < 0 || phase >= 3 || EdInteriorFlag != 0 || !CoopWorldsApart() || NetplayRole() != role || Chara == nullptr || Chara->frame == nullptr) {
        return;
    }
    if (phase == 1 && ::GameMode == ED_MODE_TALK) {
        phase = 2;
        return;
    }
    if (phase == 0 && (::GameMode != ED_MODE_WALK || ++steps < at)) {
        return;
    }
    if (phase == 1 || (phase == 2 && ::GameMode != ED_MODE_WALK)) {
        return;
    }
    if (villager_no < 0) {
        for (int i = 0; i < 10 && villager_no < 0; i++) {
            villager_no = EdVillager[i].initialized != 0 && EdVillager[i].villager_id >= 0 ? i : -1;
        }
        if (villager_no < 0) {
            return;
        }
        EdVillager[villager_no].draw_enabled = true;
    }

    CNPCharacter &villager = EdVillager[villager_no];
    sceVu0FVECTOR him;
    sceVu0FVECTOR rot;
    sceVu0FVECTOR toan;
    const bool    toward = phase == 0;

    villager.GetPosition(him);
    villager.GetRotation(rot);
    toan[0] = him[0] + 9.0f * std::sin(rot[1]);
    toan[1] = him[1];
    toan[2] = him[2] + 9.0f * std::cos(rot[1]);
    toan[3] = 1.0f;
    const float yaw = std::atan2(him[0] - toan[0], him[2] - toan[2]) + (toward ? 0.0f : 3.14159265f);

    Chara->SetPosition(toan);
    Chara->frame->SetPosition(toan);
    Chara->SetRotation(0.0f, yaw, 0.0f);
    Chara->Step();
    std::fprintf(stderr, "coop: test: town talk: Toan %s the villager %d at (%.1f %.1f %.1f): at (%.1f %.1f %.1f) facing %.2f (%s, link tick %lld)\n",
                 toward ? "before" : "turned away from", villager_no, him[0], him[1], him[2], toan[0], toan[1], toan[2], yaw, toward ? "apart" : "his talk over",
                 (long long) NetplayLinkTick());
    std::fflush(stderr);
    phase = toward ? 1 : 3;
}

void CoopTownTownTalkTick() {
    TalkHiddenSaid(); // opt8
    // opt8: the world held (every player in a talk or a menu) or running again, said as it changes (x-town-talk's line).
    if (static bool held_was = false; CoopTalkWorldHeld() != held_was) {
        held_was = !held_was;
        std::fprintf(stderr, "coop-talk: the world %s at link tick %lld\n", held_was ? "holds (every player in a talk or a menu)" : "runs", (long long) NetplayLinkTick());
        std::fflush(stderr);
    }
    OwnTownEventTick(); // coop-pt1106-batch
    // Test trace (DC_COOP_TEST_VILLAGER_TRACE=1; coop-pt1109b, x-town-hertalk-villagers): how far the town's villagers moved while her talk ran.
    if (static const bool trace = TestEnvOn("DC_COOP_TEST_VILLAGER_TRACE"); trace && EdInteriorFlag == 0) {
        static float     last[10][3] = {};
        static float     moved = 0.0f;
        static float     hers_moved = 0.0f; // (opt8: the villager she talks to, which stops for her as for him)
        static int       hers_walking = 0;  // (opt8: its ticks with its walk sequence on: retail's stop within 20 of a player turns it off)
        static bool      was_talk = false;
        static long long from = 0;

        if (g_talks[1].on && !was_talk) {
            moved = 0.0f;
            hers_moved = 0.0f;
            hers_walking = 0;
            from = NetplayLinkTick();
        }
        for (int i = 0; i < 10; i++) {
            sceVu0FVECTOR at;

            EdVillager[i].GetPosition(at);
            if (g_talks[1].on && was_talk && EdVillager[i].draw_enabled != 0 && EdVillager[i].villager_id >= 0) {
                moved += std::hypot(at[0] - last[i][0], at[2] - last[i][2]);
                hers_moved += i == g_room_event.villager ? std::hypot(at[0] - last[i][0], at[2] - last[i][2]) : 0.0f;
                hers_walking += i == g_room_event.villager && EdVillager[i].sequence_enabled != 0 ? 1 : 0;
            }
            last[i][0] = at[0];
            last[i][1] = at[1];
            last[i][2] = at[2];
        }
        if (!g_talks[1].on && was_talk) {
            std::fprintf(stderr, "coop-town: test: villagers moved %.1f during her talk (%lld link ticks), their walk held %d times and let run %d; her villager moved %.1f, its walk on %d ticks\n", moved,
                         (long long) NetplayLinkTick() - from, g_villagers_held, g_villagers_let, hers_moved, hers_walking);
            g_villagers_held = g_villagers_let = 0;
            std::fflush(stderr);
        }
        was_talk = g_talks[1].on;
    }
    // Test hook (DC_COOP_TEST_START_TRACE=1; coop-pt1106-batch, x-town-sign): each change of the town's mode or of its queued event.
    if (static const bool trace = TestEnvOn("DC_COOP_TEST_START_TRACE"); trace) {
        static int        mode_was = -99, start_was = -99;

        if (::GameMode != mode_was || start_event_no != start_was) {
            mode_was = ::GameMode;
            start_was = start_event_no;
            std::fprintf(stderr, "coop-town: test: town mode %d, queued event %d at link tick %lld\n", mode_was, start_was, (long long) NetplayLinkTick());
        }
    }
    if (EdInteriorFlag != 0) {
        return;
    }
    TownTalkApartForTest(); // coop-stage4 r6

    static bool his_was = false;
    const bool  his = HisTownTalkRuns() && g_talks[1].on;

    if (his != his_was) {
        his_was = his;
        std::fprintf(stderr, "coop-town: his town talk %s beside hers (link tick %lld)\n", his ? "runs" : "over", (long long) NetplayLinkTick());
        std::fflush(stderr);
    }

    static bool town_was = false;

    if (HisTownTalkRuns() != town_was) {
        town_was = HisTownTalkRuns();
        std::fprintf(stderr, "coop-town: his town talk %s (link tick %lld, player %d's copy, she %s)\n", town_was ? "starts" : "ends", (long long) NetplayLinkTick(),
                     NetplayLocalPlayer(), g_talks[0].on ? "walks on" : "stands");
        std::fflush(stderr);
    }

    if (CoopWorldsApart()) {
        HerTalkAtTheSplit(::GameMode == ED_MODE_WALK); // coop-pt1109f
    }
    TalkDoorForTest(); // coop-pt1109f

    // (coop-fx51c, Jep 2026-11-04: no fixed tick waits: =loaded, once a state file, the row's fixture, is put back: the same tick on
    // both linked copies, as gatekey52's DC_WORLDS_EXIT_AT=loaded; a number, from that link tick)
    static const char *const talk_spec = TestEnv("DC_COOP_TEST_TOWN_TALK");
    // (coop-pt1109b: =herloaded, only she is put before the first villager, to talk alone: nobody else's talk stops the villagers)
    static const bool        second_only = talk_spec != nullptr && std::strcmp(talk_spec, "herloaded") == 0;
    static const bool        on_load = talk_spec != nullptr && (std::strcmp(talk_spec, "loaded") == 0 || second_only);
    static const int         at = on_load ? 0 : TestEnvInt("DC_COOP_TEST_TOWN_TALK", -1);
    static int       placed = -1; // the villager, once Toan is placed
    static long long his_from = -1; // his talk's start after that
    static bool      her_done = false;

    if (placed >= 0 && his_from < 0 && HisTownTalkRuns()) {
        his_from = NetplayLinkTick();
    }
    if (at < 0 || her_done || NetplayLinkTick() < at || (on_load && !NetplayStateFileLoaded()) || CoopWorldsApart() || !Linked() || Chara == nullptr || Chara->frame == nullptr || g_town.body == nullptr ||
        (placed < 0 && ::GameMode != ED_MODE_WALK) || (placed >= 0 && (his_from < 0 || NetplayLinkTick() < his_from + 30))) {
        // (the hook waiting, every 600 link ticks: why)
        if (static long long said = -1; at >= 0 && !her_done && NetplayLinkTick() >= at && NetplayLinkTick() / 600 != said) {
            said = NetplayLinkTick() / 600;
            std::fprintf(stderr, "coop: test: town talk spot waits at link tick %lld: apart %d linked %d body %d mode %d placed %d his talk from %lld\n",
                         (long long) NetplayLinkTick(), (int) CoopWorldsApart(), (int) Linked(), g_town.body != nullptr, (int) ::GameMode, placed, his_from);
        }
        return;
    }

    // (a debug start's Norune stands its people only by the houses built: none. The test's villager is the first one set up,
    // drawn from now on, on both copies alike; each one's state said once)
    if (placed < 0) {
        int first = -1;

        for (int i = 0; i < 10; i++) {
            const CNPCharacter &villager = EdVillager[i];

            std::fprintf(stderr, "coop: test: town talk spot: villager %d set up %d id %d drawn %d\n", i, (int) villager.initialized, (int) villager.villager_id,
                         (int) villager.draw_enabled);
            first = first < 0 && villager.initialized != 0 && villager.villager_id >= 0 ? i : first;
        }
        if (first >= 0 && EdVillager[first].draw_enabled == 0) {
            EdVillager[first].draw_enabled = true;
            std::fprintf(stderr, "coop: test: town talk spot: villager %d drawn for the test\n", first);
        }
    }

    for (int i = placed >= 0 ? placed : 0; i < 10; i++) {
        CNPCharacter &villager = EdVillager[i];

        if (villager.initialized == 0 || villager.draw_enabled == 0 || villager.villager_id < 0) {
            if (placed >= 0) {
                break;
            }
            continue;
        }

        sceVu0FVECTOR him;
        sceVu0FVECTOR rot;
        sceVu0FVECTOR to;

        villager.GetPosition(him);
        villager.GetRotation(rot);

        const float side = placed < 0 && !second_only ? 0.6f : -0.6f; // (his, then hers: both in front of it, apart)

        to[0] = him[0] + 9.0f * std::sin(rot[1] + side);
        to[1] = him[1];
        to[2] = him[2] + 9.0f * std::cos(rot[1] + side);
        to[3] = 1.0f;

        const float yaw = std::atan2(him[0] - to[0], him[2] - to[2]);

        if (placed < 0 && !second_only) {
            Chara->SetPosition(to);
            Chara->frame->SetPosition(to);
            Chara->SetRotation(0.0f, yaw, 0.0f);
            Chara->Step();
            placed = i;
        } else {
            sceVu0CopyVector(g_town.pos, to);
            g_town.velocity[0] = g_town.velocity[1] = g_town.velocity[2] = 0.0f;
            g_town.body->frame->SetRotation(0.0f, yaw, 0.0f);
            g_town.body->SetRotation(0.0f, yaw, 0.0f);
            her_done = true;
            // opt8 test hook (DC_COOP_TEST_VILLAGER_WALKS=1, x-town-hertalk-villagers): no fixture's Norune has a walking villager, so the
            // one she is put before wanders by retail's own rule (EdMoveVillager's initial_motion, EdSetVillagerNextPos), on both copies
            // alike: the nearest-player stop (CoopTownNearestPlayer) then has something to stop.
            if (TestEnvOn("DC_COOP_TEST_VILLAGER_WALKS")) {
                EdVillagerInfo[i].placed = 1; // (a debug start's Norune places none: the mover skips an unplaced one)
                EdVillagerInfo[i].initial_motion = 1;
                std::fprintf(stderr, "coop: test: town talk spot: villager %d wanders (retail's walking villager)\n", i);
            }
        }
        std::fprintf(stderr, "coop: test: town talk spot: %s before villager %d at (%.1f %.1f %.1f) facing %.2f, link tick %lld, town mode %d\n",
                     her_done ? "player 2" : "Toan", i, to[0], to[1], to[2], yaw, (long long) NetplayLinkTick(), (int) ::GameMode);
        std::fflush(stderr);
        return;
    }
    her_done = true; // (no villager drawn, or it went: said by the test's missing line)
    std::fprintf(stderr, "coop: test: town talk spot: no villager drawn (link tick %lld)\n", (long long) NetplayLinkTick());
}

// coop-roomevent: around a picture of her room event drawn on his copy (its sprites, item, windows, fade), out of sight.
// coop-pt1108b (Jep's 11-08 playtest: P2 talking to the mayor, P1 opens the closet: no camera move on his screen, a black flash on hers): her talk is
// beside the room, so a room event under it is his: its camera and pictures on his copy, out of sight on hers. DC_COOP_MUTATE=hisevent_as_hers (twin): as her event.
static bool HisEventBesideHerTalk() {
    return g_room_event.on && g_talks[1].on && g_indoor.mode == 4 && !Mutated("hisevent_as_hers"); // ED_IN_MODE_EVENT
}

static bool RoomEventHide(int copy = 1) {
    if (NetplayLocalPlayer() != copy || Mutated("roomevent_shown")) {
        return false;
    }

    if (g_talk_hidden == gfx::kNullTexture) {
        g_talk_hidden = gfx::CreateRenderTarget(640, 480, true);
    }
    g_talk_previous = gfx::CurrentRenderTarget();
    gfx::SetRenderTarget(g_talk_hidden);
    return true;
}

bool CoopTownRoomEventHiddenBegin() {
    if (HisEventBesideHerTalk()) {
        return RoomEventHide(2);
    }
    return g_room_event.on && RoomEventHide();
}

// Her event's fade: from her event on, till the screen has cleared after it (its fade in runs on after the script ends).
static int g_door_fade_hidden = 0; // coop-pt1106-batch

// Whether this copy is a linked player's other than that one (a door opening together: the copy that is not its leaver's).
static bool CoopTownLocalIsNot(int player) {
    const int local = NetplayLocalPlayer();

    return local != 0 && local != player;
}

int CoopTownDoorFadeHiddenTake() {
    const int frames = g_door_fade_hidden;

    g_door_fade_hidden = 0;
    return frames;
}

bool CoopTownRoomFadeHiddenBegin() {
    // coop-pt1106-batch: a door opening together is its leaver's: its fade out of sight on the other's copy (they walk on).
    // DC_COOP_MUTATE=door_fade_shown (twin): on both screens.
    if (const int leaver = CoopWorldsDoorWindowBy(); leaver != 0 && CoopTownLocalIsNot(leaver) && !Mutated("door_fade_shown")) {
        if (g_talk_hidden == gfx::kNullTexture) {
            g_talk_hidden = gfx::CreateRenderTarget(640, 480, true);
        }
        g_talk_previous = gfx::CurrentRenderTarget();
        gfx::SetRenderTarget(g_talk_hidden);
        g_door_fade_hidden++;
        return true;
    }

    static bool hers = false;
    float       colour[4] = {};

    ::EdGetFadeColor(colour);
    // coop-pt1108b: his event's fade beside her talk is his, till it has cleared
    static bool his = false;

    his = HisEventBesideHerTalk() || (his && colour[3] > 0.0f);
    if (his) {
        hers = false;
        return RoomEventHide(2);
    }
    hers = g_room_event.on || (hers && colour[3] > 0.0f);
    return hers && RoomEventHide();
}

void CoopTownRoomEventHiddenEnd(bool hidden, bool frame_end) {
    if (!hidden) {
        return;
    }
    gfx::SetRenderTarget(g_talk_previous);
    if (frame_end) {
        g_room_event.frames_hidden++;
    }
}

static bool HidesPartner();

// coop-doorfade: while Toan's door opens (the screen fading out on it), the frames she was drawn in and the first reason
// she was not, said once the door's mode ends (stderr, DC_COOP_DEBUG; the door-fade test reads it).
static int g_doorfade_drawn = 0;
static int g_doorfade_skipped = 0;
static int g_doorfade_why = 0;
static bool g_doorfade_room = false; // coop-housefade: a room's door

// coop-housefade: Toan's door opening on this copy, a town's (the town's mode) or a room's (ED_IN_MODE_DOOR_OPEN).
static bool HisDoorOpens() {
    return EdInteriorFlag != 0 ? g_indoor.mode == 1 : ::GameMode == ED_MODE_DOOR_OPEN;
}

static void DoorFadeNote(bool drawn, int why) {
    if (HisDoorOpens()) {
        g_doorfade_room = EdInteriorFlag != 0;
        (drawn ? g_doorfade_drawn : g_doorfade_skipped)++;
        if (!drawn && g_doorfade_why == 0) {
            g_doorfade_why = why;
        }
    }
}

static void CoopTownDoorFadeSaid() {
    if (!HisDoorOpens() && g_doorfade_drawn + g_doorfade_skipped > 0) {
        if (CoopDebug()) {
            std::fprintf(stderr, "coop-town: p%d door fade: player 2 drawn %d frames, not drawn %d (first why %d: 1 not shown, 2 no body, 4 building, 8 door side), %s\n",
                         NetplayLocalPlayer(), g_doorfade_drawn, g_doorfade_skipped, g_doorfade_why, g_doorfade_room ? "a room's door" : "a town door");
            std::fflush(stderr);
        }
        g_doorfade_drawn = g_doorfade_skipped = g_doorfade_why = 0;
        g_doorfade_stood = false;
    }
}

void CoopTownDraw() {
    // (coop-gatepopup3: the other player's call apart is drawn with the town's late 2D, CoopP2TownMenuDraw, over her model; here,
    // among the characters, her model was drawn after it and stood in front of its buttons: Jep's 2026-10-28 Steam run. As
    // before with DC_COOP_MUTATE=call_under_world, the twin.)
    if (Mutated("call_under_world")) {
        coop::DrawCallQuestion();
    }
    // coop-inbuild: not on the other side of a door from where she last stepped (a frame between the door and her next step).
    // (coop-scenejoin: nor in his story scene in the room: SceneHolds)
    if (!g_town.visible || g_town.body == nullptr || HidesPartner() || g_indoor.was != (EdInteriorFlag != 0) || SceneHolds()) {
        DoorFadeNote(false, !g_town.visible ? 1 : g_town.body == nullptr ? 2 : HidesPartner() ? 4 : SceneHolds() ? 16 : 8);
        return;
    }

    // coop-roomevent: her room event hides its player as the game's own hides Toan.
    if (g_room_event.on && EdEventInfo.player_draw == 0) {
        return;
    }

    DoorFadeNote(true, 0); // coop-doorfade

    // coop-housefade test line (DC_COOP_DOORFADE_SHOT): at frame 10 of a room's door fade, where her waist comes out on this
    // copy's screen (0-1 across and down; the test's frame check samples there).
    if (static const bool shots = TestEnv("DC_COOP_DOORFADE_SHOT") != nullptr; shots && g_doorfade_room && HisDoorOpens() && g_doorfade_drawn == 10) {
        float waist[4] = {g_town.pos[0], g_town.pos[1] + 8.0f, g_town.pos[2], 1.0f};
        int   screen[4] = {};
        const bool seen = MGRotTransPers(screen, waist, 0) != 0;

        std::fprintf(stderr, "coop-town: p%d room door fade frame 10: her waist at (%.0f %.0f %.0f) on screen %d at (%.3f %.3f)\n", NetplayLocalPlayer(), waist[0],
                     waist[1], waist[2], seen ? 1 : 0, ((screen[0] - 0x6C00) >> 4) / 640.0f, (((screen[1] - GS_Y_OFFSET) >> 4) * 2) / 448.0f);
    }

    // DC_COOP_DEBUG: indoors, every 60th frame she is drawn in (the inbuild tests read it).
    if (static int drawn = 0; EdInteriorFlag != 0 && CoopDebug() && ++drawn % 60 == 1) {
        std::printf("coop-debug: p%d indoors draws player 2 at (%.1f %.1f %.1f), %d frames\n", NetplayLocalPlayer(), g_town.pos[0], g_town.pos[1], g_town.pos[2],
                    drawn);
    }

    // Its shadow first, through the town's shadow buffer, as EdDrawCharacter draws Toan's: under the
    // light raised by EdLimitShadowLight (a shorter shadow) and blended at 0x34. Under the scene's own
    // light at the dungeon's 0x40 hers fell longer and darker than his (Jep, 2026-10-24).
    // DC_COOP_MUTATE=townshadowlight draws it that old way (the town shadow test's twin).
    CTexture *shadow = TexManager.GetTexture((char *) "shadow_buff", -1);

    if (shadow != nullptr && !Mutated("townshadow")) {
        const bool old_way = Mutated("townshadowlight");
        float      light[4][4];
        float      shadow_light[4][4];
        float      colour[4][4];

        MGGetPLight(light, colour);
        sceVu0CopyMatrix(shadow_light, light);
        if (!old_way) {
            EdLimitShadowLight(shadow_light, 3.0f);
        }
        MGSetPLight(shadow_light, colour);
        TexManager.ReloadTexture(Vif1Packet, 0x16);
        MGBeginDrawShadow(*(sceGsTex0 *) &shadow->tex0);
        g_town.body->DrawShadow();
        g_town.shadow_alpha = old_way ? 0x40 : 0x34;
        MGEndDrawShadow(g_town.shadow_alpha);
        MGSetPLight(light, colour);
        g_town.shadow_light_y = shadow_light[1][0];
        g_town.scene_light_y = light[1][0];
        g_town.shadows++;
    }

    // coop-localfix (Jep's 2026-10-26: Xiao invisible on her own screen after the join, her shadow alone drawn): the renderer's
    // texture misses during her model's draw, once a second with DC_COOP_DEBUG (a test reads them).
    const unsigned long misses_before = dc::render::PortTextureMisses();

    TexManager.ReloadTexture(Vif1Packet, PartnerTextureBlock());
    g_town.body->TextureAnime(PartnerTextureBlock());
    g_town.body->Draw();

    static unsigned long misses = 0;
    static int           frames = 0;

    misses += dc::render::PortTextureMisses() - misses_before;

    // coop-houseoutframe (l-local-play -HerShotAt's "her own frame shows her", which counted white pixels where she stands beside
    // Toan and went red whenever the opening went her way and she walked alone in the field): in a frame a test writes to a
    // file, that this copy drew her model there, where her waist came out on its screen (0-1 across and down) and the
    // renderer's texture misses in that draw. The test reads the line before the frame's own.
    if (NetplayTestShotPending()) {
        float      waist[4] = {g_town.pos[0], g_town.pos[1] + 8.0f, g_town.pos[2], 1.0f};
        int        screen[4] = {};
        const bool seen = MGRotTransPers(screen, waist, 0) != 0;
        const float across = ((screen[0] - 0x6C00) >> 4) / 640.0f, down = (((screen[1] - GS_Y_OFFSET) >> 4) * 2) / 448.0f;

        std::fprintf(stderr, "coop-town: p%d her model in the test frame: drawn, waist on screen %d at (%.3f %.3f), %lu texture misses in the draw\n",
                     NetplayLocalPlayer(), seen && across >= 0.0f && across <= 1.0f && down >= 0.0f && down <= 1.0f ? 1 : 0, across, down,
                     dc::render::PortTextureMisses() - misses_before);
    }
    if (++frames % 60 == 0 && CoopDebug()) {
        std::printf("coop-town: p%d drew her model 60 frames, %lu texture misses\n", NetplayLocalPlayer(), misses);
        misses = 0;
    }

    if (g_partner.weapon_loaded) {
        g_partner.weapon->Step();
        TexManager.ReloadTexture(Vif1Packet, PartnerWeaponBlock());
        g_partner.weapon->Draw();
    }

    CoopDrawWheel(); // pad 2's character wheel, when open
}

bool CoopTownSkinsToan() {
    const bool skins = g_town_skin && g_town.visible;
    // (Not once a merge's host state is in: the game's player is player 1's Toan from there.)
    const bool p2_apart = Enabled() && (CoopWorldsApart() || PreJoinP2()) && NetplayRole() == 2 && !CoopWorldsMergeStateInHere() && !NetplayStayerMerge();
    // coop-localfix (Jep, 2026-10-26: "Xiao should not turn into toan at any point"): player 2's copy apart on a frame her
    // skin is not stepped (the frame her door or the cave's event splits the pair, her first frames back in a town, a
    // merge's link coming up) drew the game's player, Toan; it draws nobody then. DC_COOP_MUTATE=toan_flash (twin): Toan.
    const bool hide = p2_apart && !skins && !Mutated("toan_flash");

    // coop-localfix: from the town's and the rooms' draws, what player 2's copy apart shows as its player.
    // coop-localrace: and her copy waiting to join whatever draws it (PreJoinP2 off under DC_COOP_MUTATE=prejoin_toan, the
    // ~prejoin twin: Toan then, said so, where it said nothing and the -HerFirst hold waited its minute for the line).
    const bool p2_waiting = Enabled() && NetplayRole() == 2 && !NetplayLinkUp() && !CoopWorldsApart();

    if (p2_apart || p2_waiting) {
        CoopWorldsIdentity(skins ? coop::TownPlayerMember() : hide ? -1 : 0, EdInteriorFlag != 0 ? "a building" : "a town");
    }
    return skins || hide;
}

// coop-p2skin: indoors the game's player is still Toan (Chara, as EdIn_Chara), so her skin follows him there as it does
// outside (Jep, 2026-10-24: "P2 still turns into Toan when entering the building"). Linked, or player 1's copy, nobody
// is drawn beside him indoors, as before. DC_COOP_MUTATE=indoor_toan (twin): no skin indoors, Toan drawn as before.
void CoopTownInteriorStep() {
    coop::BagSizeStep(); // coop-bags (a room has no CoopTownStep)
    CoopTownDoorFadeSaid(); // coop-doorfade
    g_room_shown_before = g_town.visible; // coop-housefade
    g_town.visible = false;
    g_town_skin = false;

    if (!CoopWorldsApart()) {
        g_town_skin_said = false;
    }

    if (Enabled() && CoopWorldsApart() && NetplayRole() == 2 && !NetplayLinkUp() && Chara != nullptr && Chara->frame != nullptr &&
        SaveData != nullptr && SaveData->GetDngStatus() != nullptr && !Mutated("toan_skin") && !Mutated("indoor_toan")) {
        TownSkinStep();
    }
}

// coop-p2doors: the door she took into a building opening in town (the split's tick on, the link still up, and the
// town's walk, which steps her skin, not running): her copy's player opens it, so her skin stands in Toan's place, not
// Toan warped to the door (Jep's 2026-10-25 note 16). Player 1's copy stays outside and steps none of this, and the
// skin's state is this copy's own until a merge resyncs it. DC_COOP_MUTATE=door_toan (twin): Toan opens it, as before.
// DC_COOP_DEBUG: the frames each was drawn, once the door has opened (the mayor test reads it).
static int g_door_skins = 0;
static int g_door_toans = 0;

void CoopTownDoorOpenStep() {
    if (!Enabled() || !CoopWorldsApart() || NetplayRole() != 2) {
        return;
    }

    g_town.visible = false;
    g_town_skin = false;

    if (Chara != nullptr && Chara->frame != nullptr && pEditGround != nullptr && SaveData != nullptr && SaveData->GetDngStatus() != nullptr &&
        !Mutated("toan_skin") && !Mutated("door_toan")) {
        TownSkinStep();
    }

    // coop-housefade: her skin counted only where it stood this frame. CoopTownSkinsToan is also true when her copy draws
    // nobody (coop-localfix's toan_flash), so the twin's frames, where Toan opens the door and nobody is drawn, counted as hers
    // and w-indoor-mayor~toan passed.
    CoopTownSkinsToan(); // (its identity line, as before)
    (g_town_skin && g_town.visible ? g_door_skins : g_door_toans)++;
}

static void DoorOpenSaid() {
    if (g_door_skins + g_door_toans > 0) {
        if (CoopDebug()) {
            std::fprintf(stderr, "coop-debug: player 2's door opened with her skin %d frames, Toan %d\n", g_door_skins, g_door_toans);
            std::fflush(stderr);
        }

        g_door_skins = g_door_toans = 0;
    }
}

void CoopTownInteriorDrawn(bool skin) {
    static int skins = 0;
    static int toans = 0;

    DoorOpenSaid(); // coop-p2doors

    if (!CoopDebug() || !CoopWorldsApart() || NetplayRole() != 2) {
        return;
    }

    (skin ? skins : toans)++;

    if (skins + toans >= 120) {
        std::fprintf(stderr, "coop-debug: player 2 apart indoors drew her skin %d frames, Toan %d\n", skins, toans);
        skins = toans = 0;
    }
}

// The time of day's event (MainEditMode's change_time_event, then event 0x84: the camera climbs the village sign to the
// sky under "Adventure Day N - Dusk" and the town fades to its new light): from the tick the clock asks for it until
// the town walks again. Town state both copies step alike.
bool TimeOfDayEvent() {
    static bool asked = false;
    static bool running = false;

    bool was = running;

    if (change_time_event != 0) {
        asked = true;
    } else if (asked && ::GameMode == ED_MODE_EVENT) {
        running = true;
    } else if (::GameMode != ED_MODE_EVENT) {
        asked = running = false;
    }

    if (running != was) {
        std::printf("coop: time of day event %s (clock %.3f, town tick %d)\n", running ? "starts" : "ends", NowTime, g_town_warp_ticks);
    }

    return running;
}

namespace {

void OwnTownView(float (*view)[4], float *eye, bool walking) {
    // The time of day's event on player 2's copy: the same climb to the sky, from player 2's character. The event's
    // camera starts from Toan's, so player 2's screen used to jump to Toan and climb with him (Jep, 2026-10-20); now
    // its eye and view are moved by player 2's offset from Toan (below), so the shot climbs from where player 2
    // stands. Player 1's copy plays the event as retail does. Drawing only.
    // DC_COOP_MUTATE=time_event_view gives player 2 the event's camera unmoved again (twin).
    const bool time_event = TimeOfDayEvent() && !Mutated("time_event_view");

    // Player 2's camera stays player 2's in every town mode, not only walking: the mode is the town's, which player 1
    // drives, so a pause, menu, Georama, door, talk or event of player 1's used to cut player 2's screen to Toan's
    // camera (Jep, 2026-10-20). It holds still while player 1 is out of walking. DC_COOP_MUTATE=towncam_walk_only
    // keeps it to walking, as before (twin).
    if (time_event || !Mutated("towncam_walk_only")) {
        walking = true;
    }

    // coop-drawrng (root fix 5, Jep 2026-10-30): every copy's town draw, linked or not, takes its random numbers from the
    // draw stream (the torches' flicker, chimney smoke, culling), so what each copy drew never reaches the next tick. It
    // replaces coop-localfix's hold in linked frames only (Jep's 2026-10-26 Local playtest: OUT OF SYNC after a town join,
    // the host drawing from the game's rand while her catch-up replayed without drawing).
    HoldDrawRng();
    // coop-localfix (Jep's 2026-10-26 retest: "when P1 exits the house, P2 still warps to Toan"): between a merge's host state
    // landing on her copy and the merge itself (its catch-up's replay, about a second), she is drawn by nobody yet, and her
    // screen fell back to the game's camera: his Toan's, at his door. Her own camera holds where it was instead.
    // DC_COOP_MUTATE=merge_view_his (twin): his camera then, as before.
    const bool merge_window = NetplayLocalPlayer() == 2 && (CoopWorldsMergeStateInHere() || (CoopWorldsApart() && !Mutated("split_cam_his"))) && g_town_cam.ready &&
                              !Mutated("merge_view_his");

    // Its frames, once it ends: how many were drawn from her camera and how many from his (the tests read it).
    static int  window_own = 0, window_his = 0;
    static bool in_window = false;

    if (NetplayLocalPlayer() == 2 && CoopWorldsMergeStateInHere()) {
        in_window = true;
        (merge_window || g_town.visible ? window_own : window_his)++;
    } else if (in_window) {
        in_window = false;
        std::fprintf(stderr, "coop-town: the merge's window drew %d frames from her camera, %d from his\n", window_own, window_his);
        window_own = window_his = 0;
    }

    // coop-pt1106-batch: her own town event outdoors (Norune's fishing board): the event's camera on her copy, his own (the town's
    // MainCamera, following him on through it) on his. DC_COOP_MUTATE=roomevent_shown (twin): the event's camera on both.
    if (CoopTownOwnTownEvent() == 2 && NetplayHasPlayer() && !Mutated("roomevent_shown")) {
        if (NetplayLocalPlayer() == CoopTownOwnTownEvent()) { // (the reader's copy: hers)
            g_room_event.view = "event";
            return;
        }
        g_room_event.view = "his";
        g_town_view.active = true;
        HoldDrawRng();
        MainCamera.GetCameraMatrix(view);
        MainCamera.GetPos(eye);
        return;
    }

    // coop-roomevent: her room event plays through the game's event camera on her copy; his copy keeps his own camera (the
    // room's, which follows Toan on through it). DC_COOP_MUTATE=roomevent_shown (twin): the event's camera on both.
    if (g_room_event.on && EdInteriorFlag != 0 && NetplayHasPlayer() && !Mutated("roomevent_shown")) {
        // coop-playfix (Jep's 2026-10-31 Local playtest: "if P2 interacts with the mayor, the camera moves over to P1 and follows
        // P1"): a talk has no camera of its own indoors (edit_in's EdTalkMode is given none, as retail's: the room's camera
        // stays on its talker, who stands), and the room's here follows Toan walking on. Her talk stays on her own camera, the
        // one her talk's window is laid out from (CoopTownHerTalkStepIn). DC_COOP_MUTATE=hertalk_room_cam (twin): the room's.
        if (coop::ActorLocalIndex() != 0 && g_talks[1].on && g_town_cam.ready && g_town_cam.camera != nullptr && !Mutated("hertalk_room_cam")) {
            g_room_event.view = "hers";
            g_town_view.active = true;
            g_town_view.own = true;
            HoldDrawRng();
            g_town_cam.camera->GetCameraMatrix(view);
            g_town_cam.camera->GetPos(eye);

            // (once a talk: the w-indoor hertalk check's line)
            if (static std::int64_t said_for = -1; said_for != g_room_event.talk_tick) {
                said_for = g_room_event.talk_tick;
                std::fprintf(stderr, "coop-town: her talk drawn from her own camera, eye (%.0f %.0f %.0f), she at (%.0f %.0f)\n", eye[0], eye[1], eye[2],
                             g_town.pos[0], g_town.pos[2]);
                std::fflush(stderr);
            }
            return;
        }
        if (NetplayLocalPlayer() == 2 || g_indoor.room == nullptr || HisEventBesideHerTalk()) { // (coop-pt1108b: his event, on his screen)
            g_room_event.view = "event";
            return;
        }

        g_room_event.view = "his";
        g_town_view.active = true;
        HoldDrawRng();
        g_indoor.room->GetCameraMatrix(view);
        g_indoor.room->GetPos(eye);
        return;
    }

    // coop-scenejoin: his story scene in the room, together: her screen shows it from the game's camera (the scene's).
    if (!walking || (!g_town.visible && !merge_window) || !NetplayHasPlayer() || Chara == nullptr || SceneHolds()) {
        return;
    }

    g_town_view.active = true;
    HoldDrawRng();

    if (NetplayLocalPlayer() != 2 || Mutated("townview")) {
        return;
    }

    // coop-town: at her own Georama, its camera.
    if (CoopP2DrawCamera(view, eye)) {
        return;
    }

    // Player 2's own camera; DC_COOP_MUTATE=towncam draws Toan's camera moved by the partner's offset
    // instead, the old coupled view (twin).
    if (g_town_cam.ready && !Mutated("towncam") && !time_event) {
        sceVu0FVECTOR look;

        g_town_cam.camera->GetCameraMatrix(view);
        g_town_cam.camera->GetPos(eye);
        g_town_cam.camera->GetRef(look);
        g_town_view.own = true;
        std::copy(eye, eye + 3, g_town_view.eye);
        g_town_view.over = eye[1] - g_town.pos[1];

        static const bool debug = TestEnv("DC_COOP_DEBUG") != nullptr;
        static int        wait = 0;

        if (debug && --wait <= 0) {
            wait = 60;
            std::printf("coop-debug: town view of player 2's copy eye (%.0f %.0f %.0f) look (%.0f %.0f), p2 (%.0f %.0f)\n", eye[0], eye[1],
                        eye[2], look[0], look[2], g_town.pos[0], g_town.pos[2]);
        }

        return;
    }

    sceVu0FVECTOR toan;
    sceVu0FVECTOR offset;
    sceVu0FVECTOR turned;

    Chara->GetPosition(toan);
    offset[0] = g_town.pos[0] - toan[0];
    offset[1] = g_town.pos[1] - toan[1];
    offset[2] = g_town.pos[2] - toan[2];
    offset[3] = 0.0f;

    // The eye moves by the offset; the view, which takes world to camera, moves the world back by it.
    sceVu0ApplyMatrix(turned, view, offset);

    for (int axis = 0; axis < 3; axis++) {
        eye[axis] += offset[axis];
        view[3][axis] -= turned[axis];
    }

    static const bool debug = TestEnv("DC_COOP_DEBUG") != nullptr;
    static int        wait = 0;

    if (debug && --wait <= 0) {
        wait = 60;
        std::printf("coop-debug: town view of player 2's copy eye (%.0f %.0f %.0f) look (-), p2 (%.0f %.0f), moved by (%.0f %.0f) up %.2f\n", eye[0],
                    eye[1], eye[2], g_town.pos[0], g_town.pos[2], offset[0], offset[2], -view[1][2]);
    }
}

} // namespace

// coop-soundpt (Jep's playtest, 2026-10-30: "P2 hear's from P1s perspective in town ... p2 walking away from me and p2 hearing
// themselves walk away"): the town's placed sounds (the parts' rivers, fires and doors, EdSetSoundSrcVol; the footsteps)
// listen from the game's camera, player 1's. On player 2's copy, walking with her own camera, they listen from hers.
// Sound only: what each copy hears changes nothing either steps. DC_COOP_MUTATE=town_listener_his: his on both (twin).
void CoopTownListener(float *eye, float *dir) {
    if (NetplayLocalPlayer() != 2 || !g_town_cam.ready || g_town_cam.camera == nullptr || Mutated("town_listener_his") ||
        !coop::TownPartnerShown(nullptr, nullptr)) {
        return;
    }

    g_town_cam.camera->GetPos(eye);
    g_town_cam.camera->GetDir(dir);

    static const bool debug = TestEnv("DC_COOP_DEBUG") != nullptr;
    static int        wait = 0;

    if (debug && --wait <= 0) {
        wait = 60;
        std::printf("coop-audio: town listener of player 2's copy at (%.0f %.0f), p2 (%.0f %.0f)\n", eye[0], eye[2], g_town.pos[0], g_town.pos[2]);
    }
}

// coop-pt1108 (Jep's 11-08 playtest: "when P1 enters Georama mode, P2's listener stays frozen: she hears her steps walking away"): the town's walk alone sets the listener
// (EdSetSoundSrcVol), and his Georama stops that step on every copy. Her copy sets it from her camera each town tick, whatever his mode, while she walks there.
// Sound only. DC_COOP_MUTATE=herlistener_frozen (twin): as before.
void CoopAudioListenerAgain(); // (coop_audio.cpp)

void CoopTownListenerTick() {
    if (!Mutated("herlistener_frozen")) {
        CoopAudioListenerAgain(); // (its one gate is CoopTownListener's: her camera on her copy, his listener as it was on his)
    }
}

// DC_COOP_DEBUG on player 2's copy: at each change of the town's mode (and once a second), whose camera the town is
// drawn from: "own" (player 2's), "moved" (Toan's moved by player 2's offset: the time of day's event) or "toan", with
// how far the drawn eye is from Toan's.
namespace {
int g_frame_town_mode = -1; // the town's own mode of the frame being drawn (CoopTownView, before any swap)
}

void CoopTownView(float (*view)[4], float *eye, bool walking) {
    sceVu0FVECTOR toan_eye = {eye[0], eye[1], eye[2], 1.0f};

    g_frame_town_mode = ::GameMode;
    g_town_view.own = false;

    OwnTownView(view, eye, walking);
    // (coop-pt1109e: what this copy drew from, for the debug line: the two copies of a scene alike)
    g_drawn_eye[0] = eye[0];
    g_drawn_eye[1] = eye[1];
    g_drawn_eye[2] = eye[2];
    g_drawn_ref[0] = view[0][2];
    g_drawn_ref[1] = view[1][2];
    g_drawn_ref[2] = view[2][2];

    // coop-localfix: on player 2's copy, for the 120 frames after the pair goes apart in a town, the most her drawn eye moved
    // in one frame (a swing to his door and back shows as a jump; the tests read it).
    {
        static bool          apart_was = false;
        static int           frames = -1;
        static float         most = 0.0f;
        static sceVu0FVECTOR last = {};
        // And how far her view turned in one frame (degrees), over the 90 frames before the split (his door opening) and
        // the 120 after (Jep's 2026-10-26 retests: "P2's camera still looks towards the house for a split second").
        static float         turns[90] = {};
        static int           turn_at = 0;
        static float         turn_before = 0.0f, turn_after = 0.0f;
        static float         forward_last[3] = {};
        const bool           apart = NetplayRole() == 2 && CoopWorldsApart();
        const float          forward[3] = {view[0][2], view[1][2], view[2][2]};
        const float          dot = forward[0] * forward_last[0] + forward[1] * forward_last[1] + forward[2] * forward_last[2];
        const float          turn = std::acos(std::clamp(dot, -1.0f, 1.0f)) * 57.29578f;

        std::copy(forward, forward + 3, forward_last);
        turns[turn_at++ % 90] = turn;

        // coop-camjerk2 (DC_COOP_TEST_CAMTRACE): her drawn view each frame from 30 before the split to 120 after it, with
        // the camera it came from (hers or the game's) and where she and Toan stand.
        static const bool split_trace = TestEnv("DC_COOP_TEST_CAMTRACE") != nullptr;

        // (coop-doorflash: the split's own frame too, apart frame 0: it went untraced, and it was the one drawn from his camera)
        if (split_trace && NetplayRole() == 2 && (frames >= 0 || !apart || !apart_was) && Chara != nullptr) {
            sceVu0FVECTOR toan;
            sceVu0FVECTOR game;

            Chara->GetPosition(toan);
            MainCamera.GetPos(game);
            const CCameraFollow *hers = g_town_cam.camera != nullptr ? g_town_cam.camera : &MainCamera;

            std::fprintf(stderr, "coop-splittrace: %s frame %d eye (%.1f %.1f %.1f) fwd (%.3f %.3f %.3f) turn %.2f own %d ready %d game eye (%.1f %.1f %.1f) her (%.1f %.1f %.1f) toan (%.1f %.1f %.1f) mode %d game dist %.1f h %.1f angle %.2f hers dist %.1f h %.1f angle %.2f\n",
                         apart ? "apart" : "together", apart ? frames + 1 : 0, eye[0], eye[1], eye[2], forward[0], forward[1], forward[2], turn,
                         (int) g_town_view.own, (int) g_town_cam.ready, game[0], game[1], game[2], g_town.pos[0], g_town.pos[1], g_town.pos[2], toan[0],
                         toan[1], toan[2], (int) ::GameMode, MainCamera.distance, MainCamera.height, MainCamera.angle, hers->distance, hers->height,
                         hers->angle);
        }

        if (apart && !apart_was) {
            frames = 0;
            most = 0.0f;
            turn_before = *std::max_element(turns, turns + 90);
            turn_after = 0.0f;
        } else if (frames >= 0 && ++frames <= 120) {
            const float dx = eye[0] - last[0], dy = eye[1] - last[1], dz = eye[2] - last[2];

            most = std::max(most, std::sqrt(dx * dx + dy * dy + dz * dz));
            turn_after = std::max(turn_after, turn);
            if (frames == 120) {
                // (stderr: the test stops the copies soon after, and a stopped copy may never flush stdout: "no split line")
                std::fprintf(stderr, "coop-town: after the split her eye moved at most %.1f in a frame\n", most);
                std::fprintf(stderr, "coop-town: around the split her view turned at most %.1f degrees in a frame (%.1f before it, %.1f after)\n",
                             std::max(turn_before, turn_after), turn_before, turn_after);
                std::fflush(stderr);
                frames = -1;
            }
        }
        apart_was = apart;
        std::copy(eye, eye + 3, last);
    }

    // coop-houseenter (DC_COOP_DEBUG; Jep, 2026-10-27: P1 came into the house where she was: her screen went black a few
    // seconds, then she was invisible; and "the camera jump was when P1 entered the house"): from the frame the other copy
    // is heard where this one is (apart) to its merge, and the 120 frames after it, on each copy: the frames she was not
    // drawn in, the longest wait between two frames drawn, and how far the drawn eye moved in one frame (the tests read it).
    if (CoopDebug() && NetplayRole() != 0) {
        static int                                   phase = 0; // 1 before the merge, 2 after it
        static int                                   frames = 0, hidden = 0, after = 0, after_hidden = 0;
        static float                                 most = 0.0f;
        static sceVu0FVECTOR                         last = {};
        static std::chrono::steady_clock::time_point drawn_at{};
        static long long                             gap = 0;
        static NetplayCostTotals                     costs_last{}; // coop-ws5left
        const auto                                   now = std::chrono::steady_clock::now();
        // (coop-scenejoin: a story scene holds and hides her by design, and its camera is the scene's: those frames count as drawn and still)
        const bool scene = SceneHolds();
        const bool shown = (g_town.visible && g_town.body != nullptr && g_indoor.was == (EdInteriorFlag != 0) && !HidesPartner()) || scene;

        // coop-camjerk (Jep, 2026-10-27: "P2's camera still jerks when P1 enters the mayor's house"): and how far her view
        // turned in one frame (degrees), from his arrival to 600 frames after the merge (the eye alone missed a turn on the spot).
        static float  turn_most = 0.0f, turn_most_before = 0.0f;
        static int    turn_frame = -1, turn_frames = 0;
        static float  fwd_last[3] = {};
        const float   fwd[3] = {view[0][2], view[1][2], view[2][2]};
        const float   fdot = fwd[0] * fwd_last[0] + fwd[1] * fwd_last[1] + fwd[2] * fwd_last[2];
        const float   turned = scene ? 0.0f : std::acos(std::clamp(fdot, -1.0f, 1.0f)) * 57.29578f;
        static const bool trace = TestEnv("DC_COOP_TEST_CAMTRACE") != nullptr;

        std::copy(fwd, fwd + 3, fwd_last);
        if (phase == 0 && CoopWorldsApart() && (CoopWorldsPeerHere() || NetplayStayerMerge())) {
            phase = 1;
            frames = hidden = after = after_hidden = 0;
            most = 0.0f;
            gap = 0;
            turn_most = turn_most_before = 0.0f;
            turn_frame = -1;
            turn_frames = 0;
        } else if (phase != 0) {
            const float dx = eye[0] - last[0], dy = eye[1] - last[1], dz = eye[2] - last[2];

            most = scene ? most : std::max(most, std::sqrt(dx * dx + dy * dy + dz * dz));
            const long long waited = (long long) std::chrono::duration_cast<std::chrono::milliseconds>(now - drawn_at).count();

            // coop-ws5left: and where it went (NetplayCosts since the frame before), from 150 ms (the unloaded merge frame's 180-240)
            const NetplayCostTotals costs = NetplayCosts();
            const std::string       parts = NetplayTickPartsSay(); // coop-small1b: and inside the netplay tick

            if (waited >= 150) { // coop-lockgap: where a long wait falls (the [t] stamp of the catch-up lines)
                const double tick = costs.tick_ms - costs_last.tick_ms, wait = costs.wait_ms - costs_last.wait_ms, take = costs.take_ms - costs_last.take_ms,
                             host = costs.catch_ms - costs_last.catch_ms, send = costs.send_ms - costs_last.send_ms;

                std::fprintf(stderr, "coop-town: a %lld ms wait between frames around the merge (phase %d) [t %lld]: netplay tick %.0f ms (pads %.0f, "
                                     "catch-up host %.0f, sends %.0f), take %.0f, the rest %.0f; inside: %s\n",
                             waited, phase, (long long) (std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 10000000),
                             tick, wait, host, send, take, (double) waited - tick - take, parts.c_str());
            }
            costs_last = costs;
            gap = std::max(gap, waited);
        }
        if (phase != 0 || turn_frames > 0) {
            const int at = phase == 1 ? -frames : (phase == 2 ? after : 120 + (480 - turn_frames));

            if (phase == 1) {
                turn_most_before = std::max(turn_most_before, turned);
            } else if (turned > turn_most) {
                turn_most = turned;
                turn_frame = at;
            }
            if (trace && NetplayRole() == 2) {
                std::fprintf(stderr, "coop-camtrace: %s %d eye (%.1f %.1f %.1f) fwd (%.3f %.3f %.3f) turn %.2f tick %lld\n", phase == 1 ? "before" : "after", at,
                             eye[0], eye[1], eye[2], fwd[0], fwd[1], fwd[2], turned, (long long) NetplayLinkTick());
            }
            if (phase == 0 && --turn_frames == 0 && NetplayRole() == 2) {
                std::fprintf(stderr, "coop-town: around the merge her view turned at most %.1f degrees in a frame before it, %.1f in the 600 after "
                                     "(at frame %d after it)\n",
                             turn_most_before, turn_most, turn_frame);
            }
        }
        if (phase == 1 && !CoopWorldsApart()) {
            phase = 2;
        }
        if (phase == 1) {
            frames++;
            hidden += shown ? 0 : 1;
        } else if (phase == 2) {
            after_hidden += shown ? 0 : 1;
            if (++after == 120) {
                std::printf("coop-town: around the merge (player %d's copy, %s): before it %d frames, her hidden in %d; after it %d frames, her hidden in "
                            "%d; longest wait between frames %lld ms; the eye moved at most %.1f in a frame\n",
                            NetplayRole(), EdInteriorFlag != 0 ? "a building" : "a town", frames, hidden, after, after_hidden, gap, most);
                std::fflush(stdout);
                phase = 0;
                turn_frames = 480;
            }
        }
        drawn_at = now;
        CoopFrameBeat(phase != 0, NetplayLinkTick()); // WS5: DC_COOP_TEST_FRAME_WATCH, coop_crashlog.cpp
        std::copy(eye, eye + 3, last);
    }

    static const bool debug = CoopDebug();
    static int        mode_was = -1;
    static int        wait = 0;

    if (!debug || NetplayLocalPlayer() != 2 || !g_town.visible || (::GameMode == mode_was && --wait > 0)) {
        return;
    }

    mode_was = ::GameMode;
    wait = 60;

    float dx = eye[0] - toan_eye[0], dy = eye[1] - toan_eye[1], dz = eye[2] - toan_eye[2];
    float apart = std::sqrt(dx * dx + dy * dy + dz * dz);
    const char *whose = apart < 1.0f ? "toan" : TimeOfDayEvent() ? "moved" : "own";

    std::printf("coop-debug: p2 town camera mode %d %s, eye %.0f from Toan's, p2 (%.0f %.0f)\n", ::GameMode, whose, apart, g_town.pos[0],
                g_town.pos[2]);
}

// The sky goes round the eye it is drawn for (EdDrawSky puts the dome, sun and clouds at its camera's eye): on her copy
// drawn from her own camera, around hers. Drawn around the town's NowCamera it followed player 1's edit camera while he
// built, and her sky swung with his cursor (Jep's playtest, 2026-10-25, note 7). Drawing only.
// DC_COOP_MUTATE=georama_sky_his_camera: the town's NowCamera, as before (twin).
CCamera *CoopTownSkyCamera(CCamera *camera) {
    CCamera *sky = camera;

    if (NetplayLocalPlayer() == 2 && g_town_view.active && g_town_view.own && g_town_cam.camera != nullptr && !Mutated("georama_sky_his_camera")) {
        sky = g_town_cam.camera;
    }

    // DC_COOP_DEBUG: once a second on her copy, where the sky was drawn, her eye and the town's own mode.
    static const bool debug = CoopDebug();
    static int        wait = 0;

    if (debug && NetplayLocalPlayer() == 2 && sky != nullptr && --wait <= 0) {
        sceVu0FVECTOR at;

        wait = 60;
        sky->GetPos(at);
        std::printf("coop-town: p2 sky drawn at (%.1f %.1f %.1f), her eye (%.1f %.1f %.1f), town mode %d\n", at[0], at[1], at[2], g_town_view.eye[0],
                    g_town_view.eye[1], g_town_view.eye[2], g_frame_town_mode);
    }

    return sky;
}

static float CoopTownDoorFadeHiddenPeek() {
    float colour[4] = {};

    ::EdGetFadeColor(colour);
    return colour[3];
}

void CoopTownViewEnd(CCamera *camera) {
    // coop-pt1108b: test lines of his event beside her talk: which view each copy drew, every 20 ticks with a frame (DC_COOP_TEST_HISEVENT_SHOTS).
    if (static const char *const shots = TestEnv("DC_COOP_TEST_HISEVENT_SHOTS"); shots != nullptr && g_room_event.on && g_talks[1].on && (g_indoor.mode == 4 || CoopTownDoorFadeHiddenPeek() > 0.0f)) {
        static std::int64_t said_at = -1;
        const std::int64_t  now = NetplayLinkTick();

        if (now != said_at && now % 4 == 0) {
            char  path[512];
            float colour[4] = {};

            said_at = now;
            ::EdGetFadeColor(colour);
            std::snprintf(path, sizeof path, "%s/p%d-h%lld.png", shots, NetplayLocalPlayer(), (long long) now);
            NetplayTestShotTo(path);
            std::fprintf(stderr, "coop-town: test: his event beside her talk: player %d's copy drew view %s at link tick %lld, fade %.0f\n", NetplayLocalPlayer(), g_room_event.view, (long long) now, colour[3]);
            std::fflush(stderr);
        }
    }

    // DC_COOP_DEBUG: each change of the town's mode and of its time-of-day event, with the clock.
    static const bool debug = CoopDebug();
    static int        mode_was = -1;
    static int        event_was = -1;
    static int        frames = 0;

    frames++;

    if (debug && (::GameMode != mode_was || change_time_event != event_was)) {
        std::printf("coop-debug: town mode %d time event %d clock %.3f frame %d\n", ::GameMode, change_time_event, NowTime, frames);
        mode_was = ::GameMode;
        event_was = change_time_event;
    }

    if (!g_town_view.active) {
        if (g_town_view.rand_held) {
            g_town_view.rand_held = false;
            DrawRngLeave();
        }
        return;
    }

    g_town_view.active = false;
    g_town_view.rand_held = false;

    sceVu0FMATRIX view;
    sceVu0FVECTOR eye;

    // coop-localfix test hook (DC_COOP_TEST_DRAW_VIEW_STAYS=1, player 2's copy): her own camera's view is left as the draw's,
    // as a copy whose draw left another view would (the tick's view comes from the game's camera now: CoopTownTickView).
    if (static const bool stays = TestEnvOn("DC_COOP_TEST_DRAW_VIEW_STAYS"); stays && NetplayLocalPlayer() == 2) {
        DrawRngLeave();
        return;
    }

    CCamera *from = camera != nullptr ? camera : NowCamera; // coop-inbuild: the interior's own

    MGPortHoldTickView();
    from->GetCameraMatrix(view);
    from->GetPos(eye);
    MGPortSetViewMatrixNoCut(view, eye);
    DrawRngLeave();
}

void CoopTownSnapshotTexturesOut() {
    if (Mutated("town_model_after_rebuild")) {
        return;
    }

    // The host's partner textures, named in its texture table at its own addresses with this copy's pixels staged there:
    // out before the registry is made again (her model loads its own after it).
    TexManager.DeleteTextureBlock(PartnerTextureBlock());
    TexManager.DeleteTextureBlock(PartnerWeaponBlock());
    TexManager.CleanUpTextureList();
}

void CoopTownSnapshotApplied() {
    // coop-townstand: a state file's partner this copy had not loaded yet (CoopTownSyncApply): loaded now, at its place.
    if (g_file_load_due) {
        g_file_load_due = false;
        if (g_join_place && g_town.loaded < 0 && CoopWorldsHereFloor() == -1000 && SaveData != nullptr && pEditGround != nullptr) {
            if (!TownLoad(TownMember())) {
                g_town.loaded = -1;
            } else {
                g_town.visible = true;
            }
            std::printf("coop: her town partner loaded for the state file (loaded %d)\n", g_town.loaded);
            std::fflush(stdout);
            return;
        }
    }

    if (Mutated("town_model_after_rebuild") || g_town.loaded < 0 || g_town.body == nullptr || g_town.body->frame == nullptr) {
        return;
    }

    const int chara = g_town.loaded;

    std::printf("coop: town partner %d loaded again for the state set\n", chara);
    std::fflush(stdout);
    g_reload_in_place = true; // where she stands: the merge's own placing comes on its tick (TownMergedOutdoors)
    if (!TownLoad(chara)) {
        g_town.loaded = -1;
    }
    g_reload_in_place = false;
}

void CoopTownTickView() {
    // coop-localfix (Jep, 2026-10-26: "making whatever is camera sync dependent be less dependent"): the view the town's tick
    // reads (MGClipBox: whether its water is in view, and so whether its ripples roll the random state) set from the game's
    // own camera at the tick's start, on every linked copy alike, not left by the last draw (her own camera's, a display
    // frame's). DC_NETPLAY_MUTATE=tick_view_from_draw (twin): the draw's, as before.
    if (!NetplayHasPlayer() || NowCamera == nullptr || EdInteriorFlag != 0 || NetplayMutated("tick_view_from_draw")) {
        return;
    }

    sceVu0FMATRIX view;
    sceVu0FVECTOR eye;

    NowCamera->GetCameraMatrix(view);
    NowCamera->GetPos(eye);
    MGPortSetViewMatrixNoCut(view, eye);
}

bool CoopTownCameraView(float (*view)[4]) {
    if (NowCamera == nullptr || EdInteriorFlag != 0 || NetplayMutated("tick_view_from_draw")) {
        return false;
    }

    NowCamera->GetCameraMatrix(view);
    return true;
}

// coop-localfix (Jep's 2026-10-26 retest: "a reload for P2 when P2 was on the way to the house"; her own Georama then opened
// at another cursor on each copy): her town state is this module's, not the game's, so a join's state set left each copy
// its own: the host placed her beside his Toan as she joined, her copy kept where it had her (2 units apart at the first
// tick), and every later step went on from there. Her place travels with the state set as an extra (CoopTownSyncBytes):
// filled on the host before the take, put back on the joiner after the apply. DC_COOP_MUTATE=town_place_own (twin):
// each copy keeps its own, as before.
struct TownSync {
    sceVu0FVECTOR pos;
    sceVu0FVECTOR velocity;
    sceVu0FVECTOR ground;
    float         yaw;
    std::int32_t  airborne;
    std::int32_t  has_ground;
    std::int32_t  high_camera;
    std::int32_t  motion;
    std::int32_t  set;
    std::int32_t  presence_was;    // the presence latch (coop_party), always carried
    std::int32_t  presence_absent;
    // Her own camera (Jep's 2026-10-26 retest: the reload at the mayor's stairs): her stick walks her against its angle, and
    // her copy's camera came from before the join (its own, following her there) while the host made one afresh from his.
    std::int32_t  cam_ready;
    std::int32_t  cam_map;
    std::int32_t  cam_indoor; // coop-herexit2: a room's camera (TownCam::indoor)
    float         cam_distance;
    float         cam_swing; // coop-housecam: TownCam::swing
    unsigned char cam[sizeof(CCameraFollow)];
    // coop-houseenter: inside a building, her side of the room (placed in it) and her camera's marker among the room's, so a
    // merge by her state goes on from there on his copy as on hers (not placed again).
    std::int32_t  indoor_set;
    std::int32_t  indoor_was;
    std::int32_t  indoor_marker;
    std::int32_t  indoor_marker_hold;
    std::int32_t  stood; // coop-nopush: whether her body walls Toan's walk (Town::stood), always carried
    // coop-camlast (house-exit desync, 2026-10-31): her place on her camera's last step (TownCam::last), which her camera's
    // drift reads as her stride. Not carried, his copy kept its own stale one, so its first step after her merge turned her
    // camera (and her walk's angle) a little otherwise than hers did: her place parted ~2 units by link tick 60, OUT OF SYNC
    // by 120, whenever she walked into the merge (under load). DC_COOP_MUTATE=town_cam_last_own (twin): each copy its own.
    sceVu0FVECTOR cam_last;
    std::int32_t  visible; // coop-townstand: she was shown on the taker (a state file's load shows her only then)
};
TownSync g_town_sync;

unsigned char *CoopTownSyncBytes(std::size_t *size) {
    *size = sizeof g_town_sync;
    return reinterpret_cast<unsigned char *>(&g_town_sync);
}

void CoopTownCamTestLine(long long tick) {
    const CCameraFollow *c = g_town_cam.camera;

    std::fprintf(stderr, "tickdump: tick %lld her town: loaded %d shown %d pos %.5f %.5f %.5f vel %.5f %.5f %.5f last %.5f %.5f cam ready %d map %d dist %.5f",
                 tick, g_town.loaded, g_town.visible ? 1 : 0, g_town.pos[0], g_town.pos[1], g_town.pos[2], g_town.velocity[0], g_town.velocity[1],
                 g_town.velocity[2], g_town_cam.last[0], g_town_cam.last[2], g_town_cam.ready ? 1 : 0, g_town_cam.map, g_town_cam.distance);
    if (c != nullptr) {
        std::fprintf(stderr, " angle %.6f next %.6f angle_h %.6f follow %.5f %.5f %.5f pos %.5f %.5f %.5f ref %.5f %.5f %.5f h %.5f",
                     c->angle, c->next_angle, c->angle_h, c->follow[0], c->follow[1], c->follow[2], c->pos[0], c->pos[1], c->pos[2], c->ref[0],
                     c->ref[1], c->ref[2], c->height);
    }
    // coop-soakfix2: the game's own town camera too (player 1's view, the checksum's "view" in a town)
    const CCameraFollow &m = MainCamera;
    std::fprintf(stderr, " | main angle %.6f next %.6f angle_h %.6f follow %.5f %.5f %.5f pos %.5f %.5f %.5f ref %.5f %.5f %.5f h %.5f now %d",
                 m.angle, m.next_angle, m.angle_h, m.follow[0], m.follow[1], m.follow[2], m.pos[0], m.pos[1], m.pos[2], m.ref[0], m.ref[1],
                 m.ref[2], m.height, NowCamera == &MainCamera ? 1 : 0);
    if (Chara != nullptr) {
        const auto &mi = ((CMainChara *) Chara)->move_info;
        std::fprintf(stderr, " | toan landed %d normal %.5f %.5f %.5f", (int) mi.landed, mi.ground_poly.normal[0], mi.ground_poly.normal[1],
                     mi.ground_poly.normal[2]);
    }
    std::fprintf(stderr, "\n");
}

void CoopTownSyncTake() {
    g_town_sync = TownSync{};
    PresenceGet(&g_town_sync.presence_was, &g_town_sync.presence_absent);
    g_town_sync.stood = g_town.stood ? 1 : 0; // coop-nopush
    // coop-localfix (Jep's 2026-10-26 retest: "P1 leaves the house, P2 ... becomes invisible"): a host with no partner in town
    // (his copy came back from a building or a floor, she is joining) loads her now, where the merge's stand puts her, so the
    // state set carries her and her copy keeps hers loaded (reloaded in place after the apply, CoopTownSnapshotApplied). Her
    // copy put hers away and loaded her again ticks later, and that late load drew her shadow alone. Not shown till she counts
    // as there (her presence), on both copies alike. DC_COOP_MUTATE=host_loads_late (twin): put away there, as before.
    if (!Mutated("town_place_own") && !Mutated("host_loads_late") && g_town.loaded < 0 && Chara != nullptr && Chara->frame != nullptr &&
        EdInteriorFlag == 0 && pEditGround != nullptr && SaveData != nullptr && SaveData->GetDngStatus() != nullptr) {
        if (TownLoad(TownMember())) {
            g_town.visible = false;
            std::printf("coop: her town partner loaded for the state set (the host had none)\n");
            TownCamInit(); // and her camera, from his (so her copy draws from it till the merge, not from his)
        } else {
            g_town.loaded = -1;
        }
    }

    if (g_town_cam.ready && g_town_cam.camera != nullptr && !Mutated("town_cam_own")) {
        constexpr std::size_t from = offsetof(CCameraFollow, pos);

        g_town_sync.cam_ready = 1;
        g_town_sync.cam_map = g_town_cam.map;
        g_town_sync.cam_indoor = g_town_cam.indoor ? 1 : 0;
        g_town_sync.cam_distance = g_town_cam.distance;
        g_town_sync.cam_swing = g_town_cam.swing;
        sceVu0CopyVector(g_town_sync.cam_last, g_town_cam.last); // coop-camlast
        std::memcpy(g_town_sync.cam + from, reinterpret_cast<const unsigned char *>(g_town_cam.camera) + from, sizeof(CCameraFollow) - from);
    }

    if (EdInteriorFlag != 0) { // coop-houseenter
        g_town_sync.indoor_set = 1;
        g_town_sync.indoor_was = g_indoor.was ? 1 : 0;
        g_town_sync.indoor_marker = g_indoor.marker;
        g_town_sync.indoor_marker_hold = g_indoor.marker_hold;
    }

    if (Mutated("town_place_own") || g_town.loaded < 0 || g_town.body == nullptr || g_town.body->frame == nullptr) {
        return;
    }

    sceVu0FVECTOR rotation;

    g_town.body->frame->GetRotation(rotation);
    sceVu0CopyVector(g_town_sync.pos, g_town.pos);
    sceVu0CopyVector(g_town_sync.velocity, g_town.velocity);
    sceVu0CopyVector(g_town_sync.ground, g_town.ground);
    g_town_sync.yaw = rotation[1];
    g_town_sync.airborne = g_town.airborne;
    g_town_sync.has_ground = g_town.has_ground ? 1 : 0;
    g_town_sync.high_camera = g_town.high_camera ? 1 : 0;
    g_town_sync.motion = g_town.motion;
    g_town_sync.set = 1;
    g_town_sync.visible = g_town.visible ? 1 : 0;
}

bool CoopTownPreJoinHere(float *toan, float *yaw) {
    if (!PreJoinP2() || !g_town_skin || pEditGround == nullptr || EdInteriorFlag != 0 || !g_prejoin_toan.set || g_prejoin_toan.map != MapNo) {
        return false;
    }
    if (toan != nullptr) {
        for (int i = 0; i < 3; i++) {
            toan[i] = g_prejoin_toan.pos[i];
        }
        *yaw = g_prejoin_toan.yaw;
    }
    return true;
}

// (coop-pt1109a, Jep 11-09: for real scripted events only; a villager's dialog goes on beside the arriver's walk, CarriedTalkBecomesHers.)
// coop-stage4 4e part 2 (Jep 11-07: the joiner lands at once into whoever's talk or scene; 11-08 his answer A: the arriver is hidden through
// the stayer's scene and shown at its arrival spot when it ends): on her copy apart the game's player is her skin. Her stayer merge's
// role swap (CoopTownBecomePartner) put Toan there, so the rest of her event played on him. With her own talk or event running as
// the merge starts, the swap is due instead of done: her skin stays the game's player on every copy, her pad the game's pad
// (netplay's routing) and the arriver has no body till it ends; on the tick the town or room walks again every copy runs the swap,
// with his stand from the sender, on one link tick. This travels in the state set (kExtraSwapDefer).
// DC_WORLDS_MUTATE=merge_into_her_event / merge_into_her_town_event (twins): the swap at the merge's start, as before.
namespace {
struct SwapDefer {
    std::int32_t on;
    std::int32_t map;
    std::int32_t indoors; // coop-pt1109e: his stand is a room's (a room's coordinates are no place in the town)
    std::int32_t player;  // coop-famB: the stayer whose talk or event it is (1 up): her carried talk reads that player's pad
    float        toan[3];
    float        yaw;
};
SwapDefer g_defer{};
} // namespace

unsigned char *CoopTownSwapDeferBytes(std::size_t *size) {
    *size = sizeof g_defer;
    return reinterpret_cast<unsigned char *>(&g_defer);
}

bool CoopTownSwapDeferred() {
    return g_defer.on != 0;
}

void CoopTownSwapDefer(const float *toan, float yaw, int player) {
    g_defer.on = 1;
    g_defer.player = player;
    g_defer.map = MapNo;
    g_defer.indoors = EdInteriorFlag != 0 ? 1 : 0;
    for (int i = 0; i < 3; i++) {
        g_defer.toan[i] = toan[i];
    }
    g_defer.yaw = yaw;
    std::fprintf(stderr, "coop: her merge into her own event (town mode %d, room mode %d): the swap waits for its end, Toan to (%.0f %.0f %.0f)\n", (int) ::GameMode,
                 EdInteriorFlag != 0 ? g_indoor.mode : -1, toan[0], toan[1], toan[2]);
}

// The talk on retail's own mode, her skin the game's player, moved to where her together talk lives (CoopTownHerTalkStartIn, without a new
// start): its state and window to hers, retail's left closed and walking, her skin the partner, Toan the game's player.
static void CarriedTalkBecomesHers() {
    g_room_event.main = EdEventInfo.main_character;
    g_room_event.anime = EdEventInfo.main_texture_animation;
    g_room_event.anime_count = EdEventInfo.main_texture_animation_count;

    CNPCharacter *villager = EdNowTalkChara();

    // Her state takes the live talk, the room's is left empty; the window a copy to hers (as it is on both sides of the swap), the live one closed.
    HerTalkStateFresh();
    HerTalkSwap();
    EdTalkWindowReset();

    g_room_event.villager = villager != nullptr ? (int) (villager - EdVillager) : 0;
    // coop-famB: the talk's player is the stayer's the defer carries (coop-small2's FeedRoomEventPad feeds no pad without one: her
    // Cross never reached the talk, which never ended). DC_COOP_MUTATE=dialog_no_player (twin): left unset, as small2 left it.
    g_room_event.player = Mutated("dialog_no_player") ? 0 : g_defer.player;
    if (EdInteriorFlag == 0) {
        ::GameMode = ED_MODE_WALK;
        NowCamera = &MainCamera;
    } else {
        EdInMenuToWalk(); // (the room's own mode, this file's mirror with it)
        g_indoor.mode = 0; // ED_IN_MODE_WALK
    }

    g_defer.on = 0;
    CoopWorldsBagsBack();
    CoopTownBecomePartner(g_defer.toan, g_defer.yaw);
    g_room_event.on = true;
    g_room_event.event = 0x100;
    g_room_event.tick = NetplayLinkTick();
    g_room_event.frames_hidden = 0;
    g_room_event.talk_frames_hidden = 0;
    g_room_event.motions_said = 0;
    CoopTownHerTalkBegin();
    std::fprintf(stderr, "coop: her dialog goes on beside the walk at link tick %lld (villager %d): the arriver walks, every copy\n", (long long) NetplayLinkTick(), g_room_event.villager);
}

void CoopTownSwapDeferTick() {
    if (g_defer.on == 0) {
        return;
    }
    if (mode != 2 || MapNo != g_defer.map) { // GAME_MODE_EDIT: the event took her out of the zone: the arriver went with her
        g_defer.on = 0;
        CoopWorldsBagsBack();
        std::fprintf(stderr, "coop: her event left the zone with its swap due (mode %d): dropped\n", (int) mode);
        return;
    }
    // coop-pt1109a (Jep 11-09: a villager's dialog is no event needing shared state: "P1 left a house while P2 was in a dialog and got pulled
    // into it"; the arriver just comes out and walks, as when she talks with both there): from the pair's first together tick her talk goes on
    // as her talk beside the walk, every copy, and the swap is due no longer. Real scripted events keep the rest of this.
    // DC_COOP_MUTATE=dialog_carried (twin): the arriver held till it ends, as 4e part 2.
    if ((EdInteriorFlag == 0 ? ::GameMode == ED_MODE_TALK : CoopTownIndoorMode() == 3) && !CoopWorldsApart() && !Mutated("dialog_carried")) {
        CarriedTalkBecomesHers();
        return;
    }
    if (EdInteriorFlag == 0 ? ::GameMode != ED_MODE_WALK : !CoopTownIndoorWalks()) {
        // What her event gives is hers: her bag in the party's place through it, as on her copy apart (the state set is taken with each
        // bag home, so every copy lays hers in from its next tick). DC_COOP_MUTATE=defer_bag_his (twin): the party's, his.
        if (!Mutated("defer_bag_his")) {
            CoopWorldsBagsHers();
        }
        return;
    }
    // (it may end inside the catch-up: the swap waits for the pair's together tick, one link tick on every copy, not a replayed one)
    if (CoopWorldsApart()) {
        return;
    }
    g_defer.on = 0;
    CoopWorldsBagsBack();
    CoopWorldsBagDump("her event over");
    std::fprintf(stderr, "coop: her event is over at link tick %lld: the swap runs, every copy\n", (long long) NetplayLinkTick());
    // coop-pt1109e (Jep's 11-09 playtest, 1l: P1 walked in on her finished house's scene and came out mid-Norune): that scene ends by _GOTO_OUTSIDE, so it ends in the town, she at the
    // door; his stand was taken in the room, and a room's coordinates in the town are somewhere in the middle of it. He arrives where the scene left her: beside her at the door, by the
    // rule every arrival beside someone goes by. DC_COOP_MUTATE=scene_out_room_stand (twin): the room's stand, as before.
    if (g_defer.indoors != 0 && EdInteriorFlag == 0 && Chara != nullptr && Chara->frame != nullptr && !Mutated("scene_out_room_stand")) {
        sceVu0FVECTOR her;
        sceVu0FVECTOR turn;

        Chara->GetPosition(her);
        Chara->frame->GetRotation(turn);
        // (her right hand, kTownSpawn off, as PlaceBesideToan puts her beside him out of a building: the door's spot may stand off the ground, where TownSecondSpot finds none)
        g_defer.toan[0] = her[0] - kTownSpawn * cosf(turn[1]);
        g_defer.toan[1] = her[1];
        g_defer.toan[2] = her[2] + kTownSpawn * sinf(turn[1]);
        g_defer.yaw = turn[1];
        std::fprintf(stderr, "coop: her scene left the room: he arrives beside her at the door (%.0f %.0f %.0f)\n", g_defer.toan[0], g_defer.toan[1], g_defer.toan[2]);
    }
    CoopTownBecomePartner(g_defer.toan, g_defer.yaw);
}

bool CoopTownBecomePartner(const float *toan, float yaw) {
    if (Chara == nullptr || Chara->frame == nullptr || g_town.loaded < 0 || g_town.body == nullptr || g_town.body->frame == nullptr) {
        std::printf("coop: her merge: no skin to become the partner (loaded %d)\n", g_town.loaded);
        return false;
    }

    sceVu0FVECTOR rotation;

    Chara->GetPosition(g_town.pos);
    g_town.pos[3] = 1.0f;
    Chara->frame->GetRotation(rotation);
    g_town.velocity[0] = g_town.velocity[1] = g_town.velocity[2] = 0.0f;
    g_town.airborne = 0;
    g_town.body->frame->SetPosition(g_town.pos);
    g_town.body->frame->SetRotation(0.0f, rotation[1], 0.0f);
    g_town.body->SetPosition(g_town.pos);
    g_town.body->SetRotation(0.0f, rotation[1], 0.0f);
    g_town_skin = false;
    g_town.visible = true;
    PresenceSet(1, 0);

    // Her camera: the game's, which followed her skin, as it is now (no turn or jump on her screen). coop-houseenter: inside a
    // building, the room's (its own camera, EdIn_MainCamera), and her side of the room is this one (not placed in it again).
    constexpr std::size_t from = offsetof(CCameraFollow, pos);
    const bool            indoors = EdInteriorFlag != 0 && g_indoor.room != nullptr;
    CCameraFollow        *game = indoors ? g_indoor.room : &MainCamera;

    if (g_town_cam.camera == nullptr) {
        g_town_cam.camera = new CCameraFollow(60.0f, 20.0f, 0.0f, 4.0f);
    }
    std::memcpy(reinterpret_cast<unsigned char *>(g_town_cam.camera) + from, reinterpret_cast<const unsigned char *>(game) + from,
                sizeof(CCameraFollow) - from);
    g_town_cam.ready = true;
    g_town_cam.map = MapNo;
    g_town_cam.distance = game->GetDistance();
    g_town_cam.swing = 0.0f;
    g_town_cam.indoor = indoors;
    if (indoors) {
        g_indoor.was = true;
        g_indoor.marker = -1;
        g_indoor.marker_hold = 0;
    }

    // The game's player: Toan, where player 1's copy has him.
    sceVu0FVECTOR at = {toan[0], toan[1], toan[2], 1.0f};

    Chara->SetPosition(at);
    Chara->frame->SetPosition(at);
    Chara->SetRotation(0.0f, yaw, 0.0f);
    Chara->frame->SetRotation(0.0f, yaw, 0.0f);
    Chara->Step();

    // Standing where he comes out (his door's step): he steps aside, the one who arrived last (as a merge on the host's
    // state puts the arriver, TownMergedOutdoors), behind his own copy's black arrival hold. She stays where she stood.
    // coop-camjerk (Jep, 2026-10-27: "P2's camera still jerks when P1 enters the mayor's house"): she stepped aside, and her
    // camera, aimed at her (a room's fixed marker eye) or following her, swung with her on her screen.
    // DC_COOP_MUTATE=merge_she_steps_aside (twin): she steps aside, as before. DC_COOP_MUTATE=merge_place_at_load (twin):
    // left on him.
    if (SpawnOccupied(at, g_town.pos) && !Mutated("merge_place_at_load")) {
        float spot[3];

        if (Mutated("merge_she_steps_aside")) {
            TownSecondSpot(g_town.pos, rotation[1], at, spot);
            for (int i = 0; i < 3; i++) {
                g_town.pos[i] = spot[i];
            }
            g_town.body->frame->SetPosition(g_town.pos);
            g_town.body->SetPosition(g_town.pos);
            if (g_town_cam.ready) {
                TownCamFollow();
                g_town_cam.camera->Step(-1);
            }
            std::printf("coop: her merge: he comes in on her spot: she steps aside\n");
        } else {
            TownSecondSpot(at, yaw, g_town.pos, spot);
            for (int i = 0; i < 3; i++) {
                at[i] = spot[i];
            }
            Chara->SetPosition(at);
            Chara->frame->SetPosition(at);
            Chara->Step();
            std::printf("coop: her merge: he comes in on her spot: he steps aside\n");
        }
    }
    std::printf("coop: her merge: she is the partner at (%.0f %.0f %.0f), Toan at (%.0f %.0f %.0f)\n", g_town.pos[0], g_town.pos[1], g_town.pos[2],
                at[0], at[1], at[2]);
    // coop-uiflash: her facing as she stood apart, for the merge's facing check (DC_COOP_DEBUG).
    if (CoopDebug()) {
        std::fprintf(stderr, "coop: her facing before the merge %.2f\n", rotation[1]);
        // coop-toanvis2: whether Toan's place is a walk from where he came in (the room's side of every wall: seen), for the test.
        std::fprintf(stderr, "coop: her merge: Toan's place a walk from his door step (%.0f %.0f): %s\n", toan[0], toan[2],
                     TownStepClear(toan, at) ? "yes" : "no");
    }
    return true;
}

void CoopTownSyncApply() {
    if (Mutated("town_place_own")) {
        return;
    }

    g_town.stood = g_town_sync.stood != 0; // coop-nopush: her wall in his walk, the sender's

    // coop-localfix2: player 1's copy taking her state for her merge (it had no partner in town as it arrived): her model
    // loaded first, so her place goes onto it (and CoopTownSnapshotApplied loads it again over the new texture table).
    // (coop-dungeonjoin: a town's only; her floor merge's partner is the dungeon's, loaded at its start on both copies)
    // coop-joindesync (Jep's 2026-10-28e Steam session: OUT OF SYNC at town ticks 240, 480 and 1560 seconds after each join):
    // her own copy joining from the title had no partner in town either, so its next step loaded her "beside Toan" where he
    // stood then (the host had placed her at its take), 5 to 90 units from the host's place for her; the checksum read her
    // town place only indoors, so it saw nothing until his walk met her body on one copy alone. That load keeps the place
    // and facing the state set carries (g_join_place; loading her here, inside the apply, cut her pack's read short).
    // DC_COOP_MUTATE=join_place_late (twin): placed beside Toan at that load, as before.
    // coop-townstand (n-debug-start-save-town: town2-debug, saved with her shown, loaded at tick 60 on copies still in Queens'
    // arrival event, before their own first load of her): a state file (a fixture) loads her right after the apply
    // (CoopTownSnapshotApplied; inside it the load faulted, as a join's cut short), at the join's place, so the file's load
    // gives back the saved checksum (her shown and her place) on its own tick, not a step later.
    // DC_COOP_MUTATE=file_partner_late (twin): loaded on the next step as a join's, the load's checksum DIFFERENT.
    // Only a file that showed her, with co-op on: a single-player town file (town-single, co-op off) carries her place too
    // (the take loads her, unshown), and loading her there drew her in a game with none (f-interp-parts, d-solo-town: saved
    // DIFFERENT, train 52 45d20b26). DC_COOP_MUTATE=file_partner_any (twin): any file with her place, as 45d20b26 did.
    g_join_place = g_town_sync.set != 0 && g_town.loaded < 0 && !NetplayStayerMerge() && !Mutated("join_place_late");
    g_join_yaw = g_town_sync.yaw;
    // coop-train52 (p-p2-absent-town: a host alone loads town-both, saved with her shown and counted): an online copy with no
    // partner linked has no player 2 whatever the file says (coop-p2absent's rule), so the file neither loads her nor gives
    // back its presence latch; she comes with the join. DC_COOP_MUTATE=file_partner_alone (twin, p-p2-absent-town~filealone):
    // the file's partner and latch on a lone host too, as 45d20b26 did (Xiao counted and drawn 5 s before anyone joined).
    const bool file_alone = NetplayStateFileApplying() && NetplayConfigured() && !NetplayPartnerLinked() && !NetplayStayerMerge() &&
                            !Mutated("file_partner_alone");
    g_file_load_due = g_join_place && NetplayStateFileApplying() && !file_alone && !Mutated("file_partner_late") &&
                      ((Enabled() && g_town_sync.visible != 0) || Mutated("file_partner_any"));

    if (g_town_sync.set != 0 && g_town.loaded < 0 && NetplayStayerMerge() && CoopWorldsHereFloor() == -1000 && SaveData != nullptr && pEditGround != nullptr) {
        g_reload_in_place = true;
        if (!TownLoad(TownMember())) {
            g_town.loaded = -1;
        }
        g_reload_in_place = false;
        g_town.visible = true;
        std::printf("coop: her town partner loaded for her merge's state set (loaded %d)\n", g_town.loaded);
    }

    // Whether she counts as there: the host's latch, so her partner shows (and the town steps under his pause) from the same
    // tick on both copies, her copy's own latch from before the join no longer holding her in through its replay.
    // (coop-train52: not from a state file on an online copy with no partner linked: file_alone above)
    if (file_alone) {
        PresenceResetForSession();
    } else {
        PresenceSet(g_town_sync.presence_was, g_town_sync.presence_absent);
    }

    // Her camera as the host's (or none yet, as there: both make it from his on one tick). DC_COOP_MUTATE=town_cam_own (twin).
    if (!Mutated("town_cam_own")) {
        if (g_town_sync.cam_ready != 0) {
            constexpr std::size_t from = offsetof(CCameraFollow, pos);

            if (g_town_cam.camera == nullptr) {
                g_town_cam.camera = new CCameraFollow(60.0f, 20.0f, 0.0f, 4.0f);
            }
            std::memcpy(reinterpret_cast<unsigned char *>(g_town_cam.camera) + from, g_town_sync.cam + from, sizeof(CCameraFollow) - from);
            g_town_cam.ready = true;
            g_town_cam.map = g_town_sync.cam_map;
            g_town_cam.distance = g_town_sync.cam_distance;
            g_town_cam.swing = g_town_sync.cam_swing;
            g_town_cam.indoor = g_town_sync.cam_indoor != 0;
            if (!Mutated("town_cam_last_own")) {
                sceVu0CopyVector(g_town_cam.last, g_town_sync.cam_last); // coop-camlast
            }
        } else {
            g_town_cam.ready = false;
        }
    }

    // coop-houseenter: her merge in a room, by her state: her side of the room and her camera's marker as hers.
    if (g_town_sync.indoor_set != 0 && NetplayStayerMerge()) {
        g_indoor.was = g_town_sync.indoor_was != 0;
        g_indoor.marker = g_town_sync.indoor_marker;
        g_indoor.marker_hold = g_town_sync.indoor_marker_hold;
    }

    // The host had no partner in town (she had not joined yet on its tick): none here either, so both load her on one tick
    // and the town's own steps that ask whether she is shown (its walk under his pause or menu) agree. Her copy had her
    // shown already and the town ran under his pause there alone, rolling the random state (Jep's 2026-10-26 retest, note
    // 1: a reload right after the join).
    if (g_town_sync.set == 0) {
        if (g_town.loaded >= 0 || g_town.visible) {
            std::printf("coop: her town partner put away for the host's state set (it had none)\n");
        }
        g_town.loaded = -1;
        g_town.visible = false;
        return;
    }

    sceVu0CopyVector(g_town.pos, g_town_sync.pos);
    sceVu0CopyVector(g_town.velocity, g_town_sync.velocity);
    sceVu0CopyVector(g_town.ground, g_town_sync.ground);
    g_town.airborne = g_town_sync.airborne;
    g_town.has_ground = g_town_sync.has_ground != 0;
    g_town.high_camera = g_town_sync.high_camera != 0;

    // coop-uiflash: her facing is the sender's, as her copy had her. DC_COOP_MUTATE=merge_face_his (twin of l-local-play's facing
    // check): Toan's facing instead, so the copies part on it.
    float yaw = g_town_sync.yaw;

    if (Mutated("merge_face_his") && Chara != nullptr && Chara->frame != nullptr) {
        sceVu0FVECTOR his;

        Chara->frame->GetRotation(his);
        yaw = his[1] + 1.0f;
    }

    if (g_town.body != nullptr && g_town.body->frame != nullptr) {
        g_town.body->frame->SetPosition(g_town.pos);
        g_town.body->frame->SetRotation(0.0f, yaw, 0.0f);
        g_town.body->SetPosition(g_town.pos);
        g_town.body->SetRotation(0.0f, yaw, 0.0f);
    }

    std::printf("coop: her town place from the host's state set at (%.0f %.0f %.0f)\n", g_town.pos[0], g_town.pos[1], g_town.pos[2]);
}

// coop-twinfix (camjump's test hook): how far her town camera's eye stands from the distance its camera eases it to (its
// target), the point it circles measured flat as the split's handover does (TownCamPose's caller); -1 with no camera of hers.
// Her camera steps in the town's step on both copies alike (TownCamStep), so both copies read the same.
float CoopTownCamEaseGap() {
    if (!g_town_cam.ready || g_town_cam.camera == nullptr) {
        return -1.0f;
    }

    CCameraFollow *hers = g_town_cam.camera;
    sceVu0FVECTOR  eye;

    hers->GetPos(eye);
    const float dx = eye[0] - hers->follow[0];
    const float dz = eye[2] - hers->follow[2];

    return std::fabs(std::sqrt(dx * dx + dz * dz) - hers->distance);
}

bool CoopTownCameraNow(float *eye, float *look) {
    if (NowCamera == nullptr) {
        return false;
    }

    sceVu0FVECTOR at;

    NowCamera->GetPos(at);
    std::copy(at, at + 3, eye);
    NowCamera->GetRef(at);
    std::copy(at, at + 3, look);
    return true;
}

bool CoopTownDrawnEye(float *eye, float *over) {
    if (!g_town_view.own || NetplayLocalPlayer() != 2) {
        return false;
    }

    std::copy(g_town_view.eye, g_town_view.eye + 3, eye);
    *over = g_town_view.over;
    return true;
}

namespace coop {

bool TownPartnerShown(float *pos, int *member) {
    if (!g_town.visible || g_indoor.was != (EdInteriorFlag != 0)) { // coop-inbuild: her place is on this side of the door
        return false;
    }

    if (pos != nullptr) {
        pos[0] = g_town.pos[0];
        pos[1] = g_town.pos[1];
        pos[2] = g_town.pos[2];
    }

    if (member != nullptr) {
        *member = g_town.loaded;
    }

    return true;
}

bool TownMemberPickable(int chara) {
    return TownPickable(chara);
}

int TownPlayerMember() {
    return TownMember();
}

} // namespace coop

// coop-bagapart (Jep's 2026-10-27 playtest: "P2 has P1's inventory when P1 isn't in the area"): player 2's copy apart in a
// town or a building plays Toan with her member drawn in his place, and her bag is the game's pack (coop_worlds' SwapBags),
// so the town's own menu (Item, Weapon, Allies) showed her items under Toan's name, portrait, weapons and status. Around its
// init, step and draw her member is the party's current one, as her linked menu has it (coop_townmenu's ViewBegin); a member
// she picks there (Allies) becomes the one she plays. DC_COOP_MUTATE=apart_menu_toan (twin): Toan's, as before.
static int g_apart_menu_chara = -1;

void CoopTownApartMenuBegin() {
    CUserStatus *status = SaveData != nullptr ? (CUserStatus *) SaveData->GetDngStatus() : nullptr;
    static bool  said = false;

    g_apart_menu_chara = -1;
    if (!Enabled() || !CoopWorldsApart() || NetplayRole() != 2 || NetplayLinkUp() || status == nullptr || Mutated("apart_menu_toan")) {
        return;
    }

    g_apart_menu_chara = status->cur_chara;
    status->cur_chara = (s8) TownMember();
    if (!said) {
        said = true;
        std::fprintf(stderr, "coop-town: player 2's menu apart shows member %d (the game's player is %d)\n", (int) status->cur_chara, g_apart_menu_chara);
    }
}

void CoopTownApartMenuEnd() {
    CUserStatus *status = SaveData != nullptr ? (CUserStatus *) SaveData->GetDngStatus() : nullptr;

    coop::BagScreenTrace(); // the menu's bag and member as it has them (DC_COOP_BAGTRACE)
    if (g_apart_menu_chara < 0 || status == nullptr) {
        return;
    }

    const int picked = status->cur_chara;

    status->cur_chara = (s8) g_apart_menu_chara;
    g_apart_menu_chara = -1;
    if (picked != TownMember() && TownPickable(picked)) {
        g_partner.choice = picked;
        std::fprintf(stderr, "coop-town: player 2 picks member %d in her menu apart\n", picked);
    }
}

namespace {

// A linked copy in a town proper (not a house, whose modes are its own), the per-player pause and Georama on.
bool OwnPauseAndGeorama() {
    return Enabled() && NetplayHasPlayer() && EdInteriorFlag == 0 && !Mutated("townpause_shared");
}

bool PausedFromWalking() {
    return ::GameMode == ED_MODE_RETURN_MENU_WALK;
}

bool InGeorama(int mode) {
    return mode == ED_MODE_GEORAMA || mode == ED_MODE_GEORAMA_MENU_INIT || mode == ED_MODE_GEORAMA_MENU || mode == ED_MODE_RETURN_MENU_GEORAMA;
}

} // namespace

bool CoopTownSplitForTest() {
    static const int at = TestEnvInt("DC_COOP_TOWN_SPLIT", 0);
    static bool      done = false;

    if (at <= 0 || done || NetplayLocalPlayer() != 2 || ::GameMode != ED_MODE_WALK || g_town_warp_ticks < at) {
        return false;
    }

    done = true;
    std::printf("coop: town split test at tick %d: player 2's copy alone pauses\n", g_town_warp_ticks);
    return true;
}

std::uint32_t CoopTownModeSum() {
    // DC_COOP_MUTATE=no_town_mode_sum: none of it, as before (twin).
    if (Mutated("no_town_mode_sum")) {
        return 0;
    }

    // Packed, one field each: GameMode, then his pause, Georama and menu, then her Georama, parts menu and menu.
    std::uint32_t sum = (std::uint32_t) (::GameMode & 0xFF);

    sum |= (PausedFromWalking() ? 1u : 0u) << 8;
    sum |= (InGeorama(::GameMode) ? 1u : 0u) << 9;
    sum |= (CoopTownP1Menu() ? 1u : 0u) << 10;
    sum |= (CoopP2Building() ? 1u : 0u) << 11;
    sum |= (CoopP2MenuOpen() ? 1u : 0u) << 12;
    sum |= (CoopP2TownMenuOn() ? 1u : 0u) << 13;
    sum |= (std::uint32_t) (EdInteriorFlag & 0xFF) << 16;
    return sum;
}

// coop-townmenus: Toan while his menu is open in the linked town: he stands and breathes (his standing motion played on,
// as retail's talk does), not frozen mid-step (Jep, 2026-10-24: "P1's animation completely freezes when they're in the
// triangle menu instead of going idle"). Both copies step it. DC_COOP_MUTATE=p1menu_frozen: not stepped, as before (twin).
static void ToanIdleInMenu() {
    // coop-stage4 4b: his local menu leaves the town in its walk, his pad routed to nothing: Toan stands in the walk's own idle.
    if (CoopTownP1MenuLocal() && Chara != nullptr && Chara->frame != nullptr) {
        static int local_ticks = 0;

        if (CoopDebug() && ++local_ticks % 30 == 1) {
            std::printf("coop-town: Toan in his menu: motion %d time %.2f (the town's walk)\n", Chara->motion_no, Chara->motion_type.state.time);
        }
        return;
    }
    if (Chara == nullptr || Chara->frame == nullptr || !CoopTownP1Menu() || ::GameMode != ED_MODE_MENU || Mutated("p1menu_frozen")) {
        return;
    }

    Chara->motion_no = 0;
    Chara->motion_flags = 0;
    Chara->motion_speed = -1.0f;
    Chara->SetVelocity(CVector3_f_(0.0f, 0.0f, 0.0f));
    Chara->Step();
    Chara->ShadowStep();
    Chara->ClothStep(0);

    static int ticks = 0;

    if (CoopDebug() && ++ticks % 30 == 1) {
        std::printf("coop-town: Toan in his menu: motion %d time %.2f\n", Chara->motion_no, Chara->motion_type.state.time);
    }
}

// coop-pt1108c (Jep's 11-08 playtest: time runs with both players in Georama's parts menus; his rule, coop-menuanim: the world stops only when
// every player in the zone is paused or in a menu): since his menus went local the town stays in its walk under them (coop-stage4 4b), and the
// walk's step runs the clock. With her in a menu of hers too, or paused, the clock stands, every copy alike from the bits. Only the clock so far:
// the town's people and water still step. DC_COOP_MUTATE=menus_clock_runs (twin): it runs, as before.
static float g_clock_before = 0.0f;
static float g_now_before = 0.0f;

static bool EveryoneInMenus() {
    return OwnPauseAndGeorama() && !CoopWorldsApart() && g_town.visible && ::GameMode == ED_MODE_WALK && CoopTownP1MenuLocal() &&
           (coop::PlayerInMenu(2, coop::kActorMenuTown) || CoopP2TownPaused() || CoopP2MenuOpen()) && !Mutated("menus_clock_runs");
}

// coop-pt1109c (Jep 11-09, playtest items 1b, 1c): retail's clock (MainEditMode) steps only in the town's walk, so his talk stood the clock on
// both copies while hers did not, and it asks for the day change whatever anyone is doing, so the change's event ran over her talk. In co-op
// time runs through anyone's talk, on both copies, and the day change waits until nobody is talking: the clock goes on, so a change owed through a
// long talk plays once, with the clock as it then is (the LAST period, whatever passed). All of it reads GameMode and her talk's state, which step
// alike on both copies, so it plays on one tick. Alone, as retail. Owed is kept apart from change_time_event: that flag stands the clock and shuts
// the menus (retail's waits of moments), and a talk can be long.
// Twins (opt8: a carried talk on retail's mode alone now; its clock twin went with the together path): day_change_no_hold (no wait), day_change_each (one play for each period owed).
namespace coop {
namespace {
int g_day_change_owed = 0;      // the changes owed through a talk (state set: both copies alike)
int g_day_change_owed_loop = 0; // loop_counter as the first was owed: lower later, the town was set up again and its event went with it
} // namespace
} // namespace coop

// coop-scriptmenus: one player's own town event (the sign, its script's menus) is a talk to this rule (Jep's talk rule, pt1109c): the clock
// runs on for the others and a period's change waits for its end. It was stopped short of the hour instead (coop-pt1108f's bandaid).
// DC_COOP_MUTATE=ownevent_clock_short (twin): short of the hour, as before.
static bool OwnEventTalkClock() {
    return CoopTownOwnTownEvent() != 0 && !Mutated("ownevent_clock_short");
}

bool CoopTownDayChangeHeld() {
    return Linked() && !Mutated("day_change_no_hold") && (::GameMode == ED_MODE_TALK || CoopTalkAny() || OwnEventTalkClock()); // (opt8: anyone's talk, from its slot; coop-scriptmenus: one player's own town event)
}

// cand57 (opt8 with coop-scriptmenus): the clock steps through a talk or one player's own town event as through a walk, for the others (Jep's talk
// rule, pt1109c), unless every present player is in a talk or a menu (opt8's CoopTalkWorldHeld, Jep 11-09). opt8 keeps his talk together in the
// walk's mode, so the walk's own rule (editloop: walking and not held) covers it; this covers the town's own talk mode and the own event.
bool CoopTownClockRunsInTalk() {
    return Linked() && (::GameMode == ED_MODE_TALK || OwnEventTalkClock()) && !CoopTalkWorldHeld();
}

bool CoopTownOwnEventClockShort() {
    return CoopTownOwnTownEvent() != 0 && Mutated("ownevent_clock_short");
}

// The clock crossed a period this step. True: it waits, the caller doesn't ask for the event.
bool CoopTownDayChangeOwe(int from, int to) {
    if (!CoopTownDayChangeHeld()) {
        return false;
    }
    if (coop::g_day_change_owed == 0) {
        coop::g_day_change_owed_loop = loop_counter;
    }
    coop::g_day_change_owed++;
    std::fprintf(stderr, "coop-town: the day change waits for a talk (period %d to %d, %d owed, clock %.3f, link tick %lld)\n", from, to, coop::g_day_change_owed, NowTime,
                 (long long) NetplayLinkTick());
    return true;
}

// Each step of the clock: true when a change owed may ask for its event now.
bool CoopTownDayChangeOwedNow() {
    if (coop::g_day_change_owed == 0) {
        return false;
    }
    if (loop_counter < coop::g_day_change_owed_loop) { // (as retail's change_time_event: set up again, none)
        coop::g_day_change_owed = 0;
        return false;
    }
    if (CoopTownDayChangeHeld() || change_time_event != 0) {
        return false;
    }
    coop::g_day_change_owed = Mutated("day_change_each") ? coop::g_day_change_owed - 1 : 0;
    std::fprintf(stderr, "coop-town: nobody's talking: the day change asked for (%d still owed, clock %.3f, link tick %lld)\n", coop::g_day_change_owed, NowTime,
                 (long long) NetplayLinkTick());
    return true;
}

void CoopTownDayChangeStarted() {
    std::fprintf(stderr, "coop-town: the day change starts at link tick %lld, clock %.3f, period %d, %s\n", (long long) NetplayLinkTick(), NowTime, EdGetTime(NowTime),
                 CoopTalkAny() || ::GameMode == ED_MODE_TALK ? "someone talking" : "nobody talking");
    std::fflush(stderr);
}

// Test hook (DC_COOP_TEST_TALK_CLOCK=A,B[,her|,event]): 60 ticks into a talk (his; with ",her" hers with his over; coop-scriptmenus: with ",event" one
// player's own town event) the clock to A, 120 later to B, on both copies alike, so a talk crosses two periods in seconds (set before a period's end, the
// clock's own step crosses it); its clock said every 30 ticks.
static void TalkClockForTest() {
    COOP_TEST_SWITCH(const char *, spec, TestEnv("DC_COOP_TEST_TALK_CLOCK"));
    static float     a = -1.0f;
    static float     b = -1.0f;
    static long long from = -1;
    static int       stage = 0;
    static bool      her = spec != nullptr && std::strstr(spec, "her") != nullptr;
    static bool      own_event = spec != nullptr && std::strstr(spec, "event") != nullptr;

    if (spec == nullptr || SaveData == nullptr || (a < 0.0f && std::sscanf(spec, "%f,%f", &a, &b) != 2)) {
        return;
    }

    const bool game_player_talks = g_talks[0].on || ::GameMode == ED_MODE_TALK; // (opt8: from its slot together)
    const bool talk = own_event ? CoopTownOwnTownEvent() != 0 : her ? CoopTownHerTalkOn() && !game_player_talks : game_player_talks;

    if (!talk) {
        from = -1;
        return;
    }
    if (from < 0) {
        from = NetplayLinkTick();
    }

    const long long n = NetplayLinkTick() - from;

    if (n % 30 == 0) {
        std::fprintf(stderr, "coop-town: test: talk clock %.4f at link tick %lld\n", SaveData->GetNowTime(), (long long) NetplayLinkTick());
    }
    if ((stage == 0 && n >= 60) || (stage == 1 && n >= 180)) {
        const float to = stage == 0 ? a : b;

        NowTime = to;
        SaveData->SetNowTime(to);
        stage++;
        std::fprintf(stderr, "coop-town: test: talk clock to %.4f (%d of 2) at link tick %lld\n", to, stage, (long long) NetplayLinkTick());
    }
    std::fflush(stderr);
}

void CoopTownClockStepBegin() {
    TalkClockForTest();
    if (SaveData != nullptr) {
        g_clock_before = SaveData->GetNowTime();
        g_now_before = NowTime;
    }
}

static void ClockHoldStepEnd() {
    static bool was = false;
    const bool  holds = EveryoneInMenus();

    if (holds && SaveData != nullptr) {
        SaveData->SetNowTime(g_clock_before);
        NowTime = g_now_before;
    }
    if (holds != was) {
        was = holds;
        std::fprintf(stderr, "coop-town: the clock %s (every player in a menu or paused), link tick %lld\n", holds ? "holds" : "runs", (long long) NetplayLinkTick());
    }
}

void CoopTownStepAside() {
    ClockHoldStepEnd();
    if (OwnPauseAndGeorama() && (PausedFromWalking() || InGeorama(::GameMode) || CoopTownP1Menu())) { // coop-town: and his menu
        CoopTownStep();
    }
    if (OwnPauseAndGeorama()) {
        ToanIdleInMenu();
    }
}

// coop-localfix (Jep, 2026-10-26: no menu blocks the other player; coop-stable1's -P1Busy menu found her Cross at a door
// ignored while his town menu or pause was open: her door check ran only in the town's walk mode). Her door is looked for
// while she walks beside his menu or pause too (editloop, after CoopTownStepAside).
bool CoopTownHerDoorAside() {
    // (doors 2/2: his Georama too, where she walks on as under his pause; DC_COOP_MUTATE=aside_door_no_georama: not there, twin.)
    // coop-pt1109f (Jep 11-09: either player): and his talk: she walks on through it (CoopTownWalksThroughHisTalk), but her doors stayed shut, as his talk's mode is no walk.
    // DC_COOP_MUTATE=aside_door_no_talk (twin): not there, as before.
    return OwnPauseAndGeorama() && (PausedFromWalking() || CoopTownP1Menu() || (InGeorama(::GameMode) && !Mutated("aside_door_no_georama"))) &&
           !Mutated("aside_door_held");
}

// Her door found while his menu or pause holds the town's mode, on both copies alike: the pair splits now. His copy keeps
// his menu or pause (a building's door: GotoInterior's split keeps him outside; the cave's: its event never runs there).
// Her copy ends his menu or pause, which it was stepping out of sight, and goes on with her door. Returns whether this
// copy runs her door's event.
bool CoopTownHerDoorTakesOver(bool cave) {
    // coop-doorgate: her Cross at the cave door while B1's story needs both players: nothing starts (before any split under
    // his menu). Her building's door under his menu or pause too: its room's gate, before her copy ends his menu (GotoInterior
    // would refuse it after, his copy still in the menu).
    if (cave && CoopWorldsCaveDoorRefused(2)) {
        return false;
    }

    if (!CoopTownHerDoorAside()) {
        return true;
    }

    if (sceVu0FVECTOR pos, rot; !cave && EditMapInfo != nullptr && CoopTownPartnerPose(pos, rot)) {
        ED_EVENT_PARAM param;

        if (EdGetEvent(EditMapInfo->work.events.points, EditMapInfo->event_count, &param, pos, rot, NowTime) != 0 && param.point != nullptr &&
            param.kind == ED_EVENT_POINT_DOOR && CoopWorldsDoorRefused(param.point->destination, 2)) {
            return false;
        }
    }

    const bool menu = CoopTownP1Menu();
    const bool talk = false; // (opt8: his talk together is from its slot, the town walking: her door is the walk's own)
    const char *what = menu ? "menu" : talk ? "talk" : InGeorama(::GameMode) ? "Georama" : "pause";
    const int  me = NetplayLocalPlayer(); // (before the split: apart, the link is down)

    std::fprintf(stderr, "coop-town: player 2's door (%s) under player 1's %s\n", cave ? "the cave" : "a building", what);
    std::fflush(stderr);
    if (cave) {
        CoopWorldsSplitForHerEvent();
    }
    if (me != 2) {
        return !cave;
    }
    if (menu) {
        EdClearItemOverFlag();
        EdExitMenu();
    }
    if (talk) { // coop-pt1109f: his talk goes on on his copy; here, where it ran out of sight, its window closes with the mode
        EdTalkWindowReset();
        NowCamera = &MainCamera;
    }
    ::GameMode = ED_MODE_WALK;
    std::fprintf(stderr, "coop-town: her copy ends player 1's %s for her door\n", what);
    return true;
}

// coop-menuanim (Jep, 2026-10-24: Toan's menu "freezes all of the animations playing. it shouldnt let him walk, but it should
// still dim the screen, but since he can still get hit, he should be able to see the world animations updating behind the
// menu"): the world stops only when every player in the zone is paused or in a menu. While player 1's battle menu (drawn over
// the live town, coop_townmenu) or his pause holds the town's mode in a linked town and player 2 is in it, neither paused nor
// in her menu, both copies step the town as its walk does but for Toan (editloop: the water, wind, map animations and the
// villagers; the clock stays, so no time change starts under them). DC_COOP_MUTATE=p1menu_still (his menu over its still
// frame) and p1pause_still (his pause): the town holds, as before (twins).
bool CoopTownWorldRunsAside() {
    // (the twin, sure of its divergence: her shown body was there only while her leave's fade, kHerLeaveMs of wall clock, outlasted
    // the merge's take; a slower take found her put away (g_town.visible and CoopTownP1MenuLive's TownPartnerShown both off) and the
    // town held on both. Under the mutation the sender's town runs under his settled menu whatever her body; the joiner's holds.)
    const bool stale = Mutated("town_runs_apart") && CoopWorldsApart() && NetplayRole() != 2;

    if (!OwnPauseAndGeorama() || (!g_town.visible && !stale) || CoopP2TownPaused() || coop::PlayerInMenu(2, coop::kActorMenuTown)) {
        return false;
    }
    // coop-pt1106-lockup (x-town-stayermenu: OUT OF SYNC at 60 after a merge's take with his menu open): apart he has the town alone, and it holds
    // under his menu as a game alone's does, till the merge: her body's stale bit let it run on the sender's recorded ticks alone, and the
    // joiner's replay held it, a tick's rand apart for good. DC_COOP_MUTATE=town_runs_apart (twin): as before.
    if (CoopWorldsApart() && !stale) {
        return false;
    }
    if (stale) {
        return CoopTownP1Menu() && ::GameMode == ED_MODE_MENU && !Mutated("p1menu_still");
    }
    if (PausedFromWalking()) {
        return !Mutated("p1pause_still");
    }
    return CoopTownP1MenuLive();
}

// coop-roommenu (Jep's 2026-10-28 online playtest, both in the mayor's house: P1's menu stopped the room and the mayor;
// "it should keep moving for both"): in a room as in the town, while his battle menu is opening or open and she is in the
// room, neither paused nor in her menu, both copies step the room as its walk does but for Toan (edit_in: the room's object
// animations, water and people; Toan stands and breathes). DC_COOP_MUTATE=roommenu_still (twin): the room holds, as before.
// Decided once a tick after her step (CoopTownRoomMenuNote): the room's first step (CoopTownInteriorStep()) puts her body
// away and her own step shows her again, so asked before it she was never there.
namespace {
bool g_room_shown = false; // her body shown in the room as her step left it this tick
bool g_room_runs = false;
} // namespace

void CoopTownRoomMenuTrace();

void CoopTownRoomMenuNote() {
    g_room_shown = EdInteriorFlag != 0 && TownPartnerShown(nullptr, nullptr);
    g_room_runs = EdInteriorFlag != 0 && Linked() && CoopTownIndoorP1Menu() && g_room_shown && !CoopP2TownPaused() && !coop::PlayerInMenu(2, coop::kActorMenuTown) &&
                  !Mutated("roommenu_still") && !Mutated("p1menu_still");
    CoopTownRoomMenuTrace();
}

bool CoopTownRoomRunsAside() {
    return EdInteriorFlag != 0 && g_room_runs;
}

bool CoopTownRoomPartnerShown() {
    return EdInteriorFlag != 0 && g_room_shown;
}

// (the room's mode is edit_in's own static, not the town's ::GameMode; noted where its menu is asked about mid-tick)
namespace {
int g_room_mode_now = -1;
} // namespace

void CoopTownNoteRoomMode(int mode) {
    g_room_mode_now = mode;
}

int CoopTownRoomModeNow() {
    return EdInteriorFlag != 0 ? g_room_mode_now : -1;
}

// DC_COOP_DEBUG: each change of whether the room runs under his menu, and every 30 ticks it runs the first villager drawn
// (the mayor in i01h06) and its motion clock, for the tests (w-indoor -Case roommenu).
void CoopTownRoomMenuTrace() {
    static int was = -1;
    static int ticks = 0;
    const bool local = CoopTownP1MenuLocal(); // coop-stage4 4b: his local menu: the room walks on
    const int  now = EdInteriorFlag == 0 || !Linked() || (!CoopTownIndoorP1Menu() && !local) ? -1 : CoopTownRoomRunsAside() || local ? 1 : 0;

    if (now != was) {
        was = now;
        ticks = 0;
        if (now >= 0) {
            std::fprintf(stderr, "coop-town: the room %s under player 1's menu (%s, link tick %lld; her body shown %d on this side %d, paused %d, her menu %d)\n",
                         now ? "runs" : "holds", EdInteriorName, (long long) NetplayLinkTick(), g_town.visible ? 1 : 0, g_indoor.was ? 1 : 0,
                         CoopP2TownPaused() ? 1 : 0, CoopP2TownMenuOn() ? 1 : 0);
            std::fflush(stderr);
        }
    }
    if (now != 1 || !CoopDebug() || ++ticks % 30 != 1) {
        return;
    }
    for (int i = 0; i < 10; i++) {
        if (EdVillager[i].initialized != 0 && EdVillager[i].draw_enabled != 0) {
            std::fprintf(stderr, "coop-town: under player 1's menu in the room villager %d motion %d time %.2f, link tick %lld\n", i, EdVillager[i].motion_no,
                         EdVillager[i].motion_type.state.time, (long long) NetplayLinkTick());
            break;
        }
    }
}

void CoopTownRoomMenuDrawn(bool drawn, float projection, float room_projection) {
    static int frames = 0;

    if (EdInteriorFlag == 0 || !Linked() || (!CoopTownIndoorP1Menu() && !CoopTownP1MenuLocal())) { // (coop-stage4 4b: or his local menu)
        frames = 0;
        return;
    }
    if (CoopDebug() && frames++ % 30 == 0) {
        std::fprintf(stderr, "coop-town: room frame under player 1's menu on player %d's copy: room %s, projection %.0f (the room's %.0f), live %d, link tick %lld\n",
                     NetplayLocalPlayer(), drawn ? "drawn" : "not drawn", projection, room_projection, CoopTownP1MenuLive() ? 1 : 0,
                     (long long) NetplayLinkTick());
    }
}

namespace {
long long g_room_menu_opened = -1; // coop-roommenu test hook: the link tick his menu opened on, -1 before
} // namespace

bool CoopTownRoomMenuTestOpen() {
    static const char *const spec = TestEnv("DC_COOP_TEST_ROOM_MENU");
    static bool              done = false;
    int                      at = 0, hold = 0;

    // coop-hertalk test hook (DC_COOP_TEST_TALK_MENU=N:H): his menu opens N link ticks into her talk (both copies alike) and
    // closes H ticks after it opened, as DC_COOP_TEST_ROOM_MENU's.
    static const char *const talk_spec = TestEnv("DC_COOP_TEST_TALK_MENU");

    if (!done && talk_spec != nullptr && g_talks[1].on && std::sscanf(talk_spec, "%d:%d", &at, &hold) >= 1 && NetplayLinkTick() - g_room_event.talk_tick >= at) {
        done = true;
        g_room_menu_opened = NetplayLinkTick();
        std::fprintf(stderr, "coop-town: test: player 1's menu opens in the room %s at link tick %lld (her talk %lld ticks on)\n", EdInteriorName,
                     g_room_menu_opened, NetplayLinkTick() - g_room_event.talk_tick);
        std::fflush(stderr);
        return true;
    }

    if (done || spec == nullptr || EdInteriorFlag == 0 || !Linked() || CoopWorldsApart() || !g_town.visible || std::sscanf(spec, "%d:%d", &at, &hold) < 1 ||
        g_indoor.ticks < at) {
        return false;
    }
    done = true;
    g_room_menu_opened = NetplayLinkTick();
    std::fprintf(stderr, "coop-town: test: player 1's menu opens in the room %s at link tick %lld (her step %d)\n", EdInteriorName, g_room_menu_opened,
                 g_indoor.ticks);
    std::fflush(stderr);
    return true;
}

bool CoopTownRoomMenuTestClose() {
    static const char *const spec = TestEnv("DC_COOP_TEST_ROOM_MENU") != nullptr ? TestEnv("DC_COOP_TEST_ROOM_MENU") : TestEnv("DC_COOP_TEST_TALK_MENU");
    int                      at = 0, hold = 0;

    if (spec == nullptr || g_room_menu_opened < 0 || std::sscanf(spec, "%d:%d", &at, &hold) < 2 || NetplayLinkTick() < g_room_menu_opened + hold) {
        return false;
    }
    std::fprintf(stderr, "coop-town: test: player 1's menu closes in the room at link tick %lld\n", (long long) NetplayLinkTick());
    std::fflush(stderr);
    g_room_menu_opened = -1;
    return true;
}

namespace dc::game {
void EdGetFadeColor(float *colour); // editloop3: the editor fade
} // namespace dc::game

// coop-doorprompt: test hook DC_COOP_DOORPROMPT=N (l-local-play -CaveKey): at link tick N, both copies together, the party's
// pack gets the cave key (unless it holds one) and game flag 1 is cleared (the cave door locked, as before its key's first
// use). From then each town tick logs what changes: the town's mode (an event: the game's Use Item window), the screen
// going dark and clearing (the editor fade past 32 of 128, clear at 16), game flag 1. Read only but for the key and flag.
static long long g_door_prompt_armed = -1; // the link tick it armed on, -1 before (CoopTownDoorPromptArmed)

long long CoopTownDoorPromptArmed() {
    return g_door_prompt_armed;
}

static void DoorPromptTest() {
    static const int at = TestEnvInt("DC_COOP_DOORPROMPT", -1);
    static bool      armed = false;
    static int       game_mode = -1;
    static int       flag = -1;
    static bool      dark = false;

    if (at < 0 || SaveData == nullptr) {
        return;
    }

    const long long tick = NetplayLinkTick();

    if (!armed) {
        // (coop-cavedoorfix: and not before a state file the copy is to load has been put back, as the tests' presses)
        if (tick < at || !CoopWorldsPartnerMayStart(nullptr, -1) || NetplayStateLoadPending()) {
            return;
        }
        // coop-fixtures (2026-11-01, the root-cause rule: a step waits on what it depends on, not a tick picked with room to
        // spare): N = 0 arms once a test warp has put a player on the cave door and they have landed there (within 12 of it:
        // Toan the same height two ticks running, her town body on its ground), on both copies the same tick.
        if (at == 0) {
            // (Toan: his height steady, within 0.1 tick to tick, for 10 ticks: his ground follow moves it by a hair)
            static float  last_y = -1.0e9f;
            static int    steady = 0;
            sceVu0FVECTOR at = {0.0f, -1.0e9f, 0.0f, 1.0f};

            if (Chara != nullptr) {
                Chara->GetPosition(at); // (the town's Toan, which the gate warp moves)
            }
            const bool toan_near = std::hypot(at[0] - 910.0f, at[2] + 498.0f) < 12.0f;

            steady = toan_near && std::fabs(at[1] - last_y) < 0.1f ? steady + 1 : 0;
            const bool toan = steady >= 10;
            const bool   her = std::hypot(g_town.pos[0] - 910.0f, g_town.pos[2] + 498.0f) < 12.0f && g_town.has_ground && g_town.airborne == 0;

            last_y = toan_near ? at[1] : -1.0e9f;
            if (!toan && !her) {
                return;
            }
        }

        armed = true;
        g_door_prompt_armed = tick;
        CDngStatusData *status = SaveData->GetDngStatus();
        bool            held = false;

        for (int i = 0; status != nullptr && i < status->item_pack.num && i < 103; i++) {
            held = held || status->item_pack.item[i] == ITEM_CAVE_KEY;
        }
        if (status != nullptr && !held) {
            for (int i = 0; i < status->item_pack.num && i < 103; i++) {
                if (status->item_pack.item[i] < ITEM_DUNGEON_START) {
                    status->item_pack.item[i] = ITEM_CAVE_KEY;
                    status->item_pack.item_vol[i] = 0;
                    break;
                }
            }
        }
        SaveData->SetGameFlag(1, 0);
        std::fprintf(stderr, "doorprompt: tick %lld: test: the party holds the cave key (%s), the cave door locked (flag 1 cleared)\n", tick,
                     held ? "had it" : "given");

        // coop-doorfix: DC_COOP_DOORPROMPT_WRONG=ITEM: an item that is not the key, in the party's pack and her bag, for a
        // wrong pick (x-cave-door wrong).
        if (const int wrong = TestEnvInt("DC_COOP_DOORPROMPT_WRONG", 0); wrong > 0 && status != nullptr) {
            for (ITEM_PACK *pack : {&status->item_pack, &coop::g_bag.pack}) {
                for (int i = 0; i < 103; i++) {
                    if (pack->item[i] <= 0) {
                        pack->item[i] = (std::int16_t) wrong;
                        pack->item_vol[i] = 0;
                        pack->num = std::max<int>(pack->num, i + 1);
                        break;
                    }
                }
            }
            std::fprintf(stderr, "doorprompt: tick %lld: test: item %d in the party's pack and her bag\n", tick, wrong);
        }
    }

    // (step: this trace's own count of town ticks, which goes on apart, where the link's tick stands still)
    static long step = 0;

    step++;
    if (step % 60 == 0) {
        std::fprintf(stderr, "doorprompt: tick %lld step %ld: town mode %d\n", tick, step, ::GameMode);
    }

    float colour[4];

    EdGetFadeColor(colour);

    if (::GameMode != game_mode) {
        game_mode = ::GameMode;
        std::fprintf(stderr, "doorprompt: tick %lld step %ld: town mode %d%s\n", tick, step, game_mode, game_mode == ED_MODE_EVENT ? " (an event)" : "");
    }
    if (const int now = SaveData->GetGameFlag(1); now != flag) {
        flag = now;
        std::fprintf(stderr, "doorprompt: tick %lld step %ld: game flag 1 = %d\n", tick, step, flag);
    }
    // coop-sharedkeys: the party's cave keys, in the game's pack and the bag beside it (her copy apart swaps them).
    static int keys = -1;
    int        keys_now = 0;

    for (int i = 0; i < 103; i++) {
        keys_now += SaveData->GetDngStatus()->item_pack.item[i] == ITEM_CAVE_KEY ? 1 : 0;
        keys_now += coop::g_bag.pack.item[i] == ITEM_CAVE_KEY ? 1 : 0;
    }
    if (keys_now != keys) {
        keys = keys_now;
        std::fprintf(stderr, "doorprompt: tick %lld step %ld: the party holds %d cave keys\n", tick, step, keys);
    }
    if (!dark && colour[3] > 32.0f) {
        dark = true;
        std::fprintf(stderr, "doorprompt: tick %lld step %ld: the screen goes dark (editor fade %.0f), %s\n", tick, step, colour[3],
                     CoopWorldsApart() ? "apart" : "together");
    } else if (dark && colour[3] <= 16.0f) {
        dark = false;
        std::fprintf(stderr, "doorprompt: tick %lld step %ld: the screen clears\n", tick, step);
    }
    std::fflush(stderr);
}

void CoopTownWorldAsideTrace() {
    DoorPromptTest(); // coop-doorprompt
    // coop-pt1108c: DC_COOP_DEBUG: the clock while both parts menus are open (the tests read that it stands).
    if (static int both_ticks = 0; CoopDebug() && CoopP2MenuOpen() && ::GameMode == ED_MODE_GEORAMA_MENU && ++both_ticks % 30 == 1) {
        std::fprintf(stderr, "coop-town: both parts menus open: clock %.4f (save %.4f), link tick %lld\n", NowTime, SaveData->GetNowTime(), (long long) NetplayLinkTick());
    }
    const bool  local = CoopTownP1MenuLocal(); // coop-stage4 4b: his local menu: the town walks on (not aside)
    const char *what = PausedFromWalking() ? "pause" : (CoopTownP1Menu() && ::GameMode == ED_MODE_MENU) || local ? "menu" : nullptr;
    static int  was = -1;
    static int  ticks = 0;
    int         now = what == nullptr || !OwnPauseAndGeorama() ? -1 : CoopTownWorldRunsAside() || local ? 1 : 0;

    if (now != was) {
        was = now;
        ticks = 0;
        if (now >= 0) {
            std::printf("coop-town: the town %s under player 1's %s\n", now ? "runs" : "holds", what);
        }
    }
    // DC_COOP_DEBUG: every 30 ticks the town runs aside, the map's object animations (their values summed) and the first
    // placed villager on show (a fresh Norune has none), for the tests to read their clocks.
    if (now != 1 || !CoopDebug() || ++ticks % 30 != 1) {
        return;
    }
    if (EditMapInfo != nullptr) {
        double sum = 0.0;

        for (int i = 0; i < EditMapInfo->obj_anime_count; i++) {
            const OBJ_ANIME_SEQ *anime = &EditMapInfo->work.obj_anime[i];

            sum += std::fabs(anime->current[0]) + std::fabs(anime->current[1]) + std::fabs(anime->current[2]);
        }
        std::printf("coop-town: under player 1's %s map animations %d value %.4f\n", what, EditMapInfo->obj_anime_count, sum);
    }
    for (int i = 0; i < 10; i++) {
        if (EdVillagerInfo[i].placed != 0 && EdVillager[i].near_camera && EdVillager[i].frame != nullptr && EdVillager[i].CheckDraw() != 0) {
            std::printf("coop-town: under player 1's %s villager %d motion time %.2f at (%.1f %.1f)\n", what, i,
                        EdVillager[i].motion_type.state.time, EdVillager[i].pos[0], EdVillager[i].pos[2]);
            break;
        }
    }
}

// A builder is not in the world, as retail's Toan in build mode (Jep, 2026-10-21: players vanish at the Georama as
// before): player 2's copy leaves Toan out while the town (player 1) builds, both copies leave her body out while she
// builds. DC_COOP_MUTATE=georama_builders_shown: both drawn, as before (twin of t-p2-georama-both's vanish check).
static bool g_toan_builds = false;
static int  g_vanish_logged = -1;

bool CoopTownHidesToan() {
    return g_toan_builds && !Mutated("georama_builders_shown");
}

static bool HidesPartner() {
    return CoopP2Building() && !Mutated("georama_builders_shown");
}

static void LogVanish() {
    int now = (CoopTownHidesToan() ? 1 : 0) | (HidesPartner() ? 2 : 0);

    if (now != g_vanish_logged) {
        g_vanish_logged = now;
        std::printf("coop-town: p%d draws Toan %s, player 2 %s\n", NetplayLocalPlayer(), (now & 1) ? "hidden (building)" : "shown",
                    (now & 2) ? "hidden (building)" : "shown");
    }
}

int CoopTownDrawnMode(int mode) {
    int drawn = mode;

    g_toan_builds = NetplayLocalPlayer() == 2 && InGeorama(mode);
    LogVanish();

    if (NetplayLocalPlayer() == 2 && OwnPauseAndGeorama() && (InGeorama(mode) || CoopTownP1Menu()) && g_town.visible) { // coop-town: and his menu
        drawn = ED_MODE_WALK;
    }
    // coop-menuanim: his menu over the live town, on both copies (each draws it after the town's frame, in walk mode: the
    // frame's draw reads this tick's note).
    CoopTownP1MenuNoteFrame();
    if (CoopTownP1MenuLive()) {
        drawn = ED_MODE_WALK;
    }

    // coop-p1parts: his local parts menu shut on his copy, its close command on the way: his Georama, no gap of no picture.
    if (CoopP1PartsMenuShutHere()) {
        drawn = ED_MODE_GEORAMA;
    }

    // Her own Georama, on her copy only.
    if (NetplayLocalPlayer() == 2 && CoopP2Building()) {
        drawn = CoopP2DrawnMode() >= 0 ? CoopP2DrawnMode() : drawn;
    }

    // DC_COOP_DEBUG: each change of the mode player 2's copy draws in.
    static const bool debug = CoopDebug();
    static int        town_was = -1;
    static int        drawn_was = -1;

    if (debug && NetplayLocalPlayer() == 2 && (mode != town_was || drawn != drawn_was)) {
        town_was = mode;
        drawn_was = drawn;
        std::printf("coop-debug: p2 town draws mode %d in town mode %d%s\n", drawn, mode, CoopTownHidesPause() ? ", no pause" : "");
    }

    return drawn;
}

// The builder sees the other player walk (Jep, 2026-10-24: "still being able to see the other player walking around
// when you're in georama mode"). A Georama frame draws no characters, as retail's; both copies step the walker each tick
// all the same (her body in CoopTownStepAside, Toan in the town's own step), so this only draws. Builders stay out
// (2026-10-21). DC_COOP_MUTATE=georama_walker_hidden: nobody drawn, as before (twin).
void CoopGeoramaDrawWalker() {
    int local = NetplayLocalPlayer();

    if (local == 0 || !OwnPauseAndGeorama() || Mutated("georama_walker_hidden")) {
        return;
    }

    float at[4] = {0.0f, 0.0f, 0.0f, 1.0f};

    if (local == 1) {
        // His copy, he builds: her body, unless she builds too.
        if (!g_town.visible || g_town.body == nullptr || HidesPartner()) {
            return;
        }

        CoopTownDraw();
        sceVu0CopyVector(at, g_town.pos);
    } else {
        // Her copy, she builds (her frame is the Georama's): Toan, unless the town builds too.
        if (!CoopP2Building() || InGeorama(g_frame_town_mode) || Chara == nullptr || Chara->frame == nullptr) {
            return;
        }

        EdDrawCharacter(Chara, 3, 0, nullptr, nullptr, 1, nullptr);
        Chara->GetPosition(at);
    }

    static int drawn = 0;

    if (++drawn % 60 == 1) {
        std::printf("coop-georama: p%d drew the walker (player %d) at (%.0f %.0f), %d frames\n", local, 3 - local, at[0], at[2], drawn);
    }
}

bool CoopTownHidesBuildHelp() {
    // The Georama's help bar (its system message: "build", the part's request) is the builder's: on player 2's copy
    // while player 1 builds, on player 1's while she builds at her own (Jep's playtest, 2026-10-21: both saw it).
    // DC_COOP_MUTATE=townpause_shared shows it on both again (as the pause's twin).
    int  local = NetplayLocalPlayer();
    // coop-georamabar: and on player 1's copy the frames just after she closed hers, while her bar is still up (it drew
    // for one frame: Jep, 2026-10-27). DC_COOP_MUTATE=georama_bar_left: those frames show it again (t-georama-bar's twin).
    bool left = CoopP2HelpLeftFrame() && local == 1 && !InGeorama(g_frame_town_mode) && !Mutated("georama_bar_left");
    // Both building: each keeps the bar (its text is whoever's step set it last).
    bool hide = (local == 2 && OwnPauseAndGeorama() && InGeorama(g_frame_town_mode) && !CoopP2Building()) ||
                (local == 1 && (CoopP2Building() || left) && !InGeorama(g_frame_town_mode) && !Mutated("townpause_shared"));

    static const bool debug = CoopDebug();
    static bool       was = false;

    if (debug && hide != was) {
        was = hide;
        std::printf("coop-debug: p%d town %s the Georama's help bar\n", local, hide ? "hides" : "shows");
    }
    if (debug && left) {
        std::printf("coop-debug: p1 town hides her help bar a frame after her Georama closed\n");
    }

    return hide;
}

namespace dc::game {
int CheckEditToWalk(float *position); // editloop.cpp
} // namespace dc::game

void CoopTownTestApartUnbuild() {
    static const int  after = TestEnvInt("DC_COOP_TEST_UNBUILD_APART", 0);
    // coop-localplay (DC_COOP_TEST_UNBUILD_APART_FROM_PEER=1): the T ticks count from the first tick her copy hears his in her
    // town, so he is there while it is open whatever the load (houseout-georama~twin: on a loaded PC he came out 1500 ticks
    // late, after it had closed, and merge_in_georama had nothing to merge into).
    static const bool from_peer = TestEnvOn("DC_COOP_TEST_UNBUILD_APART_FROM_PEER");
    static int        ticks = 0;
    static bool       heard = false;

    if (after <= 0 || GameMode != ED_MODE_GEORAMA || EdInteriorFlag != 0 || Chara == nullptr) {
        ticks = 0;
        heard = false;
        return;
    }
    if (from_peer && !heard) {
        heard = CoopWorldsPeerHere();
        if (!heard) {
            return;
        }
        std::fprintf(stderr, "coop: test: her Georama apart: player 1 heard in her town, it closes %d ticks on\n", after);
    }
    if (++ticks != after) {
        return;
    }

    // As her Select there does (EditLoop's way from Georama to walking): Toan's body, hers apart, to the cursor's spot.
    sceVu0FVECTOR at;

    // (the seed's spot by the gate is off the build area, where her Select would not let her out: she stands where she stood)
    if (CheckEditToWalk(at) == 0) {
        Chara->GetPosition(at);
    }
    move_count = 30;
    GameMode = ED_MODE_WALK;
    Chara->SetPosition(at);
    MainCamera.SetAngleSoon(EditCamera.GetAngle());
    EditCamera.GetPos(at);
    MainCamera.SetPos(at);
    EditCamera.GetRef(at);
    MainCamera.SetRef(at);
    std::fprintf(stderr, "coop: test: her Georama apart closes after %d ticks\n", after);
}

void CoopTownTraceSystemMes() {
    static const bool debug = CoopDebug();
    static int        was = -1;

    if (debug && SystemMesNo > 0 && SystemMesNo != was) {
        std::printf("coop-debug: p%d town draws system message %d beside the hidden help bar\n", NetplayLocalPlayer(), SystemMesNo);
    }

    was = SystemMesNo;
}

bool CoopTownHidesPause() {
    return NetplayLocalPlayer() == 2 && OwnPauseAndGeorama() && PausedFromWalking();
}

// coop-doorprompt: the cave key picked at the cave door (coop_interact's town picker), both copies on the same tick: the
// door opens for both (game flag 1, which event 130 sets as it takes the key and reads to let a player in: inferred from
// its calls) and the player who used it is returned, so the walk splits the pair and runs the door's event on that
// player's copy alone (editloop's CaveDoorKey). 0: none this tick.
// coop-doorfix: together, the key first walks its user in beside the game (CoopTownCaveEntry); the user is given (walked
// true) on the tick that walk ends, for the split and the jump. A Cross at the opened door starts the same walk
// (CoopWorldsDungeonEventAlone).
int CoopTownCaveKeyUsed(bool *walked) {
    *walked = false;

    for (int player = 1; player <= 2; player++) {
        if (CaveAct &act = g_cave_act[player - 1]; act.kind == kCaveEntry && act.done) {
            act = CaveAct{};
            if (player == TownResident()) {
                chara_mode &= ~2;
            }
            *walked = true;
            return player;
        }
    }

    const int user = coop::TownCaveKeyUsed();

    if (user == 0 || SaveData == nullptr) {
        return 0;
    }

    SaveData->SetGameFlag(1, 1);
    std::fprintf(stderr, "coop-town: player %d used the cave key at the cave door: the door opens (flag 1) for both\n", user);
    std::fflush(stderr);
    return CoopTownCaveEntry(user) ? 0 : user;
}
