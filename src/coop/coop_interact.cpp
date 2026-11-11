#include "coop/game_ns.hpp"
#include "coop/coop.hpp"
#include "coop/netplay.hpp"
#include "coop/coop_opener.hpp" // coop-opt7 step 3: her own chest and Atla on her copy's engine
#include "coop/coop_actor.hpp" // coop-opt7e: the partner's player number (ActorNumber)

#include <SDL3/SDL.h>
#include <libvu0.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem> // coop-keyhide: the keyring hook's go file
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "battlemenu/internal.hpp" // coop-keypick: item names
#include "btitem.hpp"          // coop-keypick: PortItemSelectResult
#include "btactstatus.hpp"
#include "text/caption_text.hpp"
#include "dngstatusdata.hpp"
#include "clsmes.hpp"
#include "menu_save.hpp"
#include "randomitem.hpp"
#include "rect.hpp"
#include "render/texture_registry.hpp" // coop-usebands: the key window's pictures in the registry
#include "savedata.hpp"
#include "shot_freefuncs.hpp"
#include "snd.hpp"
#include "btitem.hpp"
#include "btmisc.hpp"
#include "camerafollow.hpp"
#include "character.hpp"
#include "clothread.hpp"
#include "collision.hpp"
#include "collisiondata.hpp"
#include "dataalloc.hpp"
#include "dataread.hpp"
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
#include "language.h" // coop-usebands2: LANG_JAPANESE
#include "main.hpp"     // coop-usebands2: SystemMes
#include "memcard.hpp" // coop-p2keygui: DrawMenuWaku, DrawMenuObjectVibe
#include "mglib.hpp"
#include "motionmodel.hpp" // coop-openercam: her opening's camera
#include "monstorunit.hpp"
#include "input/mouse_collision.hpp"
#include "runscript.hpp"   // coop-keydoor: the key door's script, run beside the game
#include "btsysscript.hpp" // coop-keydoor: its object handles
#include "shot_utils.hpp"
#include "platform/config.hpp"
#include "platform/input.hpp"
#include "texture.hpp"
#include "textureanime.hpp"
#include "userstatus.hpp"

#include "coop/coop_core.hpp"
#include "coop/coop_camtest.hpp" // coop-dungeonp2
#include "coop/coop_combat.hpp" // coop: combat
#include "coop/coop_interact.hpp"
#include "coop/coop_inventory.hpp" // coop-inventory
#include "coop/coop_menu.hpp"      // coop-keypick: MenuNotePrompt
#include "coop/coop_party.hpp"
#include "coop/coop_prompt.hpp"
#include "coop/coop_worlds.hpp" // coop-keyring: CoopWorldsApart
#include "coop/coop_message.hpp" // coop-itemprompt
#include "coop/coop_audio.hpp"   // coop-atlaopen: her presentations' special sounds
#include "coop/coop_camera.hpp"  // coop-skeldoor: CoopCameraObject (her refusal turns to her camera)
#include "coop/coop_town.hpp"    // coop-doorfix: CoopTownCaveRefusal
#include "edit.hpp"              // coop-skeldoor: EdEventInfo (the refusal's character and camera)

namespace dc::game {
int keyCtrl(float x, float y, MOTION_INFO *motion);

extern CFrameVu1 *CharaFrame;
extern std::string BtCfgCash; // coop-chestfix: the floor's map name, as MapLoad last loaded it (a char[64] at 6ec3b808)
extern int        statusAlarmRate;
extern s32        gameTask;
extern s32        iventMarker;
extern s32        mode; // coop-doorprompt: the town door's picker closes outside a town

int createAttachVolume(int item_no, int dungeon);

// coop-keydoor: editloop3.cpp's event interpreter (game/editloop3.hpp; the port's EditLoop3_EdEventScript before 9418a550
// made the fork's own one the only one), whose ext table the key door's own interpreter borrows.
extern CRunScript EdEventScript;
// sysmes.cpp's system message state (repin: out of its anonymous namespace for coop).
extern int SystemMesNo;
extern int SystemMesInputKey;
extern CDungeonMap UraDungeonMap; // coop-gatekeyno: the floor's back map (gameloop.cpp), for LogFloorGateKey's line
} // namespace dc::game

namespace coop {

namespace {

bool g_her_script_start = false; // coop-skeldoor: the script starting now is her Cross's (Interact)

// The gate keys DunMoveChara gives through its "got the key" presentation (BtGetGateKey_Init).
bool GateKey(int item) {
    switch (item) {
        case ITEM_DRAN_S_CREST:
        case ITEM_SHINY_STONE:
        case ITEM_RED_BERRY:
        case ITEM_HOOK:
        case ITEM_KING_S_SLATE:
        case ITEM_GUN_POWDER:
        case ITEM_CLOCK_HANDS:
        case ITEM_POINTY_CHESTNUT:
        case ITEM_BLACK_KNIGHT_CREST:
            return true;
    }

    return false;
}

// Each character's chest motions, as the player's chest presentation loads them (BtGetTreasurebox*).
const char *const kChestPacks[6] = {"dun/mainchara/c01d_ex00.chr", "dun/mainchara/c04b_ex00.chr", "dun/mainchara/c06b_ex00.chr",
                                    "dun/mainchara/c05a_ex00.chr", "dun/mainchara/c10b_ex00.chr", "dun/mainchara/c18a_ex00.chr"};
constexpr int kChestPackBytes = 512 * 1024;
constexpr int kChestQuads = 0x10000;

// The opening motions: small chest, large chest (the presentation's 0x2A and 0x2C).
constexpr int kOpenSmall = 0x2A;
constexpr int kOpenBig = 0x2C;
// coop-actorswap: her Atla's pose, the chest set's standing grab of the item (big chest: no kneel).
constexpr int kAtlaGrab = 0x2C;
// coop-atlahold: the same set's held pose after it, item up (retail's big chest goes on to it at the grab's end)
constexpr int kAtlaHold = 0x2D;
constexpr int kOpenTicksMax = 150;

// How many frames motion has in whichever of the partner's motion sets holds it (the chest motions are
// an added set, not the base one HasMotion reads), 0 when none.
int ChestMotionFrames(int motion) {
    int             index = -1;
    tagMOTION_TYPE *type = g_partner.chara->GetMotionParam(motion, &index, nullptr, nullptr, nullptr);

    if (type == nullptr || index < 0) {
        return 0;
    }

    return std::max(0, (int) (type->motion_info[index].end - type->motion_info[index].start));
}

// coop-actorswap (Jep's 2026-10-30 playtest: her small chest's opening "sometimes just has her in the end pose"): each motion
// set keeps its own clock (the chest set is an added one), and CCharacter::Step starts a motion from its first frame only when
// it is not the one that set last played. Her second small chest's 0x2A found the chest set still on 0x2A, ended, so it stood
// in the end pose. A presentation's motion therefore restarts from its first frame (flag 4, as retail's 'restart').
// DC_COOP_MUTATE=chestrestart: no restart, as before (twin).
int ChestPlayFlags(int flags) {
    return Mutated("chestrestart") ? flags : flags | 4;
}

// Where the set that holds motion stands: its frame now, and the motion's first and last frames. False when none holds it.
bool ChestMotionAt(int motion, float *now, int *start, int *end) {
    int             index = -1;
    tagMOTION_TYPE *type = g_partner.chara != nullptr ? g_partner.chara->GetMotionParam(motion, &index, nullptr, nullptr, nullptr) : nullptr;

    if (type == nullptr || index < 0) {
        return false;
    }

    *now = type->state.time;
    *start = (int) type->motion_info[index].start;
    *end = (int) type->motion_info[index].end;
    return true;
}

// Adds the partner's chest motions (base2.cfg of its _ex00 pack) as an extra motion set, in an arena
// of their own that is rewound for each member, so the body, weapon and the player are untouched.
bool LoadChestMotions() {
    int chara = g_partner.loaded_chara;

    if (g_partner.ex_chara == chara) {
        return true;
    }

    if (g_partner.ex_pack == nullptr) {
        g_partner.ex_pack = reinterpret_cast<u_int *>(new (std::align_val_t{64}) u_char[kChestPackBytes]);
        g_partner.ex_model = new (std::align_val_t{64}) u_char[static_cast<size_t>(kChestQuads) * 16];
    }

    int size = 0;

    if (LoadFile((char *) kChestPacks[chara], g_partner.ex_pack, &size) == 0 || size <= 0 || size > kChestPackBytes) {
        std::fprintf(stderr, "coop: cannot load %s\n", kChestPacks[chara]);
        return false;
    }

    g_partner.ex_arena.base = g_partner.ex_model;
    g_partner.ex_arena.buffer = g_partner.ex_model;
    g_partner.ex_arena.used = 0;
    g_partner.ex_arena.limit = kChestQuads;
    g_partner.chara->DeleteExtendMotion();
    g_partner.chara->LoadPackData(g_partner.ex_pack, (char *) "base2.cfg", &g_partner.ex_arena, &g_partner.ex_arena);
    g_partner.ex_chara = chara;

    if (CoopDebug()) {
        CCharacter *partner = g_partner.chara;

        for (int i = 0; i < CHARA_MOTION_MAX; i++) {
            if (partner->motion[i] != nullptr) {
                std::printf("coop-debug: motion set %d: %d to %d, info %p\n", i, partner->motion_start[i], partner->motion_end[i],
                            (void *) partner->motion[i]->motion_info);
            }
        }

        for (int m : {0x24, 0x2A, 0x2B, 0x2C, 0x2D}) {
            int index = -1;
            int set = -1;
            tagMOTION_TYPE *type = partner->GetMotionParam(m, &index, nullptr, nullptr, &set);

            std::printf("coop-debug: motion %#x: set %d index %d frames %d to %d\n", m, set, index,
                        type != nullptr && index >= 0 ? type->motion_info[index].start : -1,
                        type != nullptr && index >= 0 ? type->motion_info[index].end : -1);
        }
    }

    std::printf("coop: partner chest motions %s, %d quadwords, small %d big %d\n", kChestPacks[chara], g_partner.ex_arena.used,
                ChestMotionFrames(kOpenSmall), ChestMotionFrames(kOpenBig));
    return true;
}

// Retail's presentation (BtGetTreasureboxSmall_Loop, BtGetTreasureboxBig_Loop) stands the opener's frame
// on the box, unturned: the opening motion itself carries the body out to the box's front (+z), facing
// it, and the presentation then leaves the opener 10 in front, turned -PI to face the box. The partner's
// motions are the same ones, so it opens the same way: it stands 10 in front of the box and, while the
// motion plays, its frame is on the box (PoseBusy). Turning the frame toward the box from where the
// partner stood instead carried the body into the box, facing back (Jep's Xiao). DC_COOP_MUTATE=chestturn
// does that, for the test's twin.
constexpr float kOpenFront = 10.0f;

// The opening's pose as drawn, for the log line the chest test reads (LogPose): the box, the opening's
// length, the body (the skeleton's root) halfway through it, and the ticks left until the standing check.
struct OpenPose {
    float         box[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    int           ticks = 0;
    bool          sampled = false;
    sceVu0FMATRIX body;
    int           after = 0;
    int           swings = 0; // coop-chestface: her swing count at the close (LogPose: none since)
};

OpenPose g_pose;

// The skeleton's root (the partner frame's first child) in the world; false without one.
bool BodyMatrix(sceVu0FMATRIX matrix) {
    CFrame *bone = g_partner.chara != nullptr && g_partner.chara->frame != nullptr ? g_partner.chara->frame->child : nullptr;

    if (bone == nullptr) {
        return false;
    }

    bone->GetLWMatrix(matrix);
    return true;
}

// How far (-1 to 1) direction points from `from` toward the box, on the ground.
float TowardBox(const float *from, const float *direction) {
    float dx = g_pose.box[0] - from[0];
    float dz = g_pose.box[2] - from[2];
    float a = sqrtf(dx * dx + dz * dz);
    float b = sqrtf(direction[0] * direction[0] + direction[2] * direction[2]);

    return a > 0.0f && b > 0.0f ? (dx * direction[0] + dz * direction[2]) / (a * b) : 0.0f;
}

// Logs, from the bones, where the opening put the body and which way it faced: the body halfway through
// the opening (its distance from the box, which side) and its facing toward the box (the standing facing,
// the frame's turn, put in the root bone's axes as standing and taken out again as opening); then where
// the partner stands after it and whether it faces the box. Logging only: nothing of the game changes.
void LogPose() {
    sceVu0FMATRIX idle;

    if (!g_pose.sampled || !BodyMatrix(idle)) {
        std::printf("coop: partner chest pose: no sample\n");
        return;
    }

    float rotation[4];

    g_partner.chara->frame->GetRotation(rotation);

    float facing[3] = {sinf(rotation[1]), 0.0f, cosf(rotation[1])};
    float body_facing[3] = {0.0f, 0.0f, 0.0f};

    for (int axis = 0; axis < 3; axis++) {
        float length = idle[axis][0] * idle[axis][0] + idle[axis][1] * idle[axis][1] + idle[axis][2] * idle[axis][2];
        float along = length > 0.0f ? (facing[0] * idle[axis][0] + facing[2] * idle[axis][2]) / length : 0.0f;

        for (int k = 0; k < 3; k++) {
            body_facing[k] += along * g_pose.body[axis][k];
        }
    }

    const float *body = g_pose.body[3];
    float        bx = body[0] - g_pose.box[0];
    float        bz = body[2] - g_pose.box[2];
    float        sx = idle[3][0] - g_pose.box[0];
    float        sz = idle[3][2] - g_pose.box[2];

    std::printf("coop: partner chest pose: chara %d box (%.1f %.1f) body (%.1f %.1f) %.1f from it, %s side, facing %.2f toward it; "
                "stands %.1f from it, facing %.2f toward it; %d swings since the close\n",
                g_partner.loaded_chara, g_pose.box[0], g_pose.box[2], body[0], body[2], sqrtf(bx * bx + bz * bz),
                bz > 0.0f ? "front" : "back", TowardBox(body, body_facing), sqrtf(sx * sx + sz * sz), TowardBox(idle[3], facing),
                g_partner.swings - g_pose.swings);
}

// coop-openercam: her opening's camera, as retail's presentation frames his (Jep's 2026-10-27 playtest: "P2 should get the
// same chest opening animation P1 gets, with whatever character she plays, shown only on her screen"). Retail's camera
// rides the "cam" and "int" frames of the opening's effect model (igetfx, igetfxb in dun/pack/maindat.pac: setCameraPassData
// in BtGetTreasurebox*_Loop); the partner gets a copy of those models of her own, stood on her box, so his presentation's
// (itemOpenSmallFx, itemOpenBigFx) is never touched. Loaded and stepped on both copies alike; only her copy draws from the
// camera (CoopViewBegin). Heap memory, outside the snapshot's state set.
struct OpeningFx {
    u_int         *pack = nullptr;
    u_char        *model = nullptr;
    CDataAlloc2<1> arena;
    CMotionModel  *fx[2] = {nullptr, nullptr};
    MOTION_INFO    info[2] = {{10, 80, 0.5f, 0}, {10, 80, 0.5f, 0}};
    int            playing = -1; // 0 small, 1 large, -1 none
    bool           failed = false;
    // coop-atlaopen: the item she found, held up over the box (retail's itemBoxModel), on the copy that draws her opening:
    // its files, its model and texture (block kItemBlock), its full size, how it bobs and the scale it was last drawn at.
    u_int         *item_files = nullptr;
    u_char        *item_model = nullptr;
    CDataAlloc2<1> item_arena;
    CFrame        *item = nullptr;
    float          item_full = 1.0f;
    float          item_bob = -PI;
    float          item_drawn = 0.0f;
    // coop-chestfix (Jep's 2026-10-29 playtest, parked 5: her chest's opening "sits on its final frame"): retail draws the opening
    // box itself (itemOpenSmall / itemOpenBig: dun/etc/itemget_s and itemget_b in the floor's .mpd, lid opening by its motion) in
    // place of the floor's box, whose lid it sets open at once (lid_angle -30). Her copy keeps its own of both models, loaded
    // from the floor's .mpd (BtCfgCash) on her first opening there, and draws them at her opening's frame. Draw-only, her copy.
    u_char        *box_pack = nullptr;
    u_char        *box_model = nullptr;
    CDataAlloc2<1> box_arena;
    CMotionModel  *box[2] = {nullptr, nullptr};
    MOTION_INFO    box_info[2] = {{10, 80, 0.5f, 0}, {10, 80, 0.5f, 0}};
    char           box_map[64] = {};
    float          box_drawn = 0.0f;
    float          glow_drawn = 0.0f; // coop-chestseen: the glow's effect frame as last drawn on this copy (0 none)
    int            hidden_box = -1; // the floor's box left out of this frame's DrawItemBox (-1 none), its used flag
    s32            hidden_used = 0;
};

constexpr int kFxPackBytes = 2 * 1024 * 1024;
constexpr int kBoxPackBytes = 6400000; // (MapLoad's limit for a floor's .mpd)
constexpr int kBoxQuads = 0x20000;
constexpr int kFxQuads = 0x40000;

// coop-atlaopen (Jep's 2026-10-27 playtest: her chest has "no item-acquired sound and the animation ends at once instead of
// holding on the acquired screen like P1's"): retail's opening runs by its effect's frames (0.5 a tick from 10): the lid's
// sound and the acquired jingle (special set 2) at 19.5 (small) or 14.5 (large), the item grows over the box from 50 to 55,
// and at 79 the opener holds the item up (motion 0x2B / 0x2D) with its "acquired" message until Cross or Circle. Hers
// counts the same ticks (g_partner.open_tick, simulation state), so both copies agree on them. DC_COOP_MUTATE=chestquick:
// her opening as before (the item's message and sound at once, done after the opening motion's length); chestitem: no item.
constexpr int FxTick(float frame) {
    return (int) ((frame - 10.0f) / 0.5f);
}
constexpr int kOpenSoundSmall = FxTick(19.5f);
constexpr int kOpenSoundBig = FxTick(14.5f);
constexpr int kOpenHold = FxTick(79.0f);
constexpr int kItemBlock = 0x3D; // (no other user on a floor: the Atla's is 0x3E, the stand-in's 0x3C)
constexpr int kItemFileBytes = 1024 * 1024;
constexpr int kItemQuads = 0x10000;

OpeningFx *g_fx = nullptr;

void StepOpeningFx();
bool DrawsPartnerPresentation();
bool ShowsPartnerChest();
bool LoadOpeningBox();

bool LoadOpeningFx() {
    if (g_fx == nullptr) {
        g_fx = new OpeningFx();
    }

    if (g_fx->fx[0] != nullptr || g_fx->failed) {
        return !g_fx->failed;
    }

    g_fx->pack = reinterpret_cast<u_int *>(new (std::align_val_t{64}) u_char[kFxPackBytes]);
    g_fx->model = new (std::align_val_t{64}) u_char[static_cast<size_t>(kFxQuads) * 16];

    int size = 0;

    if (LoadFile((char *) "dun/pack/maindat.pac", g_fx->pack, &size) == 0 || size <= 0 || size > kFxPackBytes) {
        std::fprintf(stderr, "coop: cannot load dun/pack/maindat.pac for the opening's camera\n");
        g_fx->failed = true;
        return false;
    }

    g_fx->arena.base = g_fx->model;
    g_fx->arena.buffer = g_fx->model;
    g_fx->arena.used = 0;
    g_fx->arena.limit = kFxQuads;

    const char *names[2] = {"igetfx", "igetfxb"};

    for (int i = 0; i < 2; i++) {
        g_fx->fx[i] = new CMotionModel();
        g_fx->fx[i]->LoadPack(g_fx->pack, (char *) names[i], &g_fx->arena, &g_fx->arena, &g_fx->info[i], 0);
    }

    std::printf("coop: partner opening camera models loaded, %d quadwords\n", g_fx->arena.used);
    return true;
}

// Stands her copy of the effect on the box at the start of its motion, as BtGetTreasurebox*_Loop's step 1 stands his.
void StartOpeningFx(const float *box, bool big) {
    if (!LoadOpeningFx()) {
        return;
    }

    int           i = big ? 1 : 0;
    CMotionModel *fx = g_fx->fx[i];
    sceVu0FVECTOR at = {box[0], box[1], box[2], 1.0f};

    fx->frame->SetPosition(at);
    fx->frame->SetRotation(0.0f, 0.0f, 0.0f);
    fx->current_motion = 0;
    fx->motion.state.time = 10.0f;
    fx->Step();
    g_fx->playing = i;
    StepOpeningFx();

    if (ShowsPartnerChest() && !Mutated("chestlid")) { // coop-chestfix; coop-chestseen: every copy
        LoadOpeningBox();
    }
}

// Each tick of her opening: the effect's next frame (coop-opt7 step 4: its camera, coop-openercam's on her copy, is gone: her
// copy shows retail's own presentation).
void StepOpeningFx() {
    if (g_fx == nullptr || g_fx->playing < 0) {
        return;
    }

    CMotionModel *fx = g_fx->fx[g_fx->playing];

    // coop-atlaopen: none past 79, where retail's stops and holds (stepped on, the effect's motion ran out at 80 and started
    // over at 10, its camera and the item's growth with it).
    if (fx->motion.state.time < 79.0f || Mutated("chestquick")) {
        fx->Step();
    }
}

// coop-chestfix: her copy's opening boxes, from the floor's .mpd as MapLoad loads retail's (false: none to draw).
bool LoadOpeningBox() {
    if (g_fx == nullptr || BtCfgCash.empty()) {
        return false;
    }

    if (g_fx->box[0] != nullptr && std::strcmp(g_fx->box_map, BtCfgCash.c_str()) == 0) {
        return true;
    }

    if (g_fx->box_pack == nullptr) {
        g_fx->box_pack = new (std::align_val_t{64}) u_char[kBoxPackBytes];
        g_fx->box_model = new (std::align_val_t{64}) u_char[static_cast<size_t>(kBoxQuads) * 16];
    }

    g_fx->box[0] = g_fx->box[1] = nullptr;
    std::snprintf(g_fx->box_map, sizeof(g_fx->box_map), "%s", BtCfgCash.c_str());

    char path[96];
    int  size = 0;

    std::snprintf(path, sizeof(path), "dun/mpd_pack/%s.mpd", BtCfgCash.c_str());

    if (LoadFile(path, g_fx->box_pack, &size) == 0 || size <= 0 || size > kBoxPackBytes) {
        std::fprintf(stderr, "coop: cannot load %s for her opening's box\n", path);
        return false;
    }

    g_fx->box_arena.base = g_fx->box_model;
    g_fx->box_arena.buffer = g_fx->box_model;
    g_fx->box_arena.used = 0;
    g_fx->box_arena.limit = kBoxQuads;

    const char *names[2] = {"dun/etc/itemget_s/d01i02m", "dun/etc/itemget_b/d01i01m"};

    for (int i = 0; i < 2; i++) {
        g_fx->box[i] = new CMotionModel();
        g_fx->box[i]->LoadPack(reinterpret_cast<u_int *>(g_fx->box_pack), (char *) names[i], &g_fx->box_arena, &g_fx->box_arena, &g_fx->box_info[i], 0);
    }

    std::printf("coop: partner opening boxes loaded from %s, %d quadwords\n", path, g_fx->box_arena.used);
    return true;
}

// Whether this copy draws the partner's own presentations (her chest's and Atla's): her copy. (Unlinked, the one screen
// is player 1's view, as before.)
bool DrawsPartnerPresentation() {
    return NetplayLocalPlayer() == 2;
}

// coop-chestseen (Jep's 2026-11-03 ask: player 1 sees her chest open, the item rising with its light and floating there until
// her Cross): her chest's opening box, glow and item are drawn on every copy; only its camera stays her copy's
// (CoopViewChoose: OpeningCamera on local player 2). Draw-only, so the copies still step alike. DC_COOP_MUTATE=chestfx_hers:
// her copy alone, as before (twin).
bool ShowsPartnerChest() {
    return DrawsPartnerPresentation() || !Mutated("chestfx_hers");
}

// coop-atlaopen: the item she found, as retail's presentation loads it (BtGetTreasurebox*_Init: its .mds and .img; a
// weapon laid as its owner's pose has it), over the box at a hundredth of its size. On every copy (coop-chestseen).
void LoadOpeningItem(const float *box, bool big, int item_no) {
    if (!ShowsPartnerChest() || Mutated("chestitem") || !LoadOpeningFx()) {
        return;
    }

    if (g_fx->item_files == nullptr) {
        g_fx->item_files = reinterpret_cast<u_int *>(new (std::align_val_t{64}) u_char[2 * kItemFileBytes]);
        g_fx->item_model = new (std::align_val_t{64}) u_char[static_cast<size_t>(kItemQuads) * 16];
    }

    char   model_path[64];
    char   texture_path[64];
    int    model_size = 0;
    int    texture_size = 0;
    u_int *texture = reinterpret_cast<u_int *>(reinterpret_cast<u_char *>(g_fx->item_files) + kItemFileBytes);

    g_fx->item = nullptr;
    g_fx->item_drawn = 0.0f;
    BtGetItemNamePath(model_path, texture_path, item_no);

    if (LoadFile(model_path, g_fx->item_files, &model_size) == 0 || model_size <= 0 || model_size > kItemFileBytes ||
        LoadFile(texture_path, texture, &texture_size) == 0 || texture_size <= 0 || texture_size > kItemFileBytes) {
        std::fprintf(stderr, "coop: cannot load %s for her chest's item\n", model_path);
        return;
    }

    g_fx->item_arena.base = g_fx->item_model;
    g_fx->item_arena.buffer = g_fx->item_model;
    g_fx->item_arena.used = 0;
    g_fx->item_arena.limit = kItemQuads;
    SetTempTexture(kItemBlock, (char *) texture);
    g_fx->item = LoadMDSFile(g_fx->item_files, &g_fx->item_arena, 0, NULL, NULL);

    if (g_fx->item == nullptr) {
        return;
    }

    sceVu0FVECTOR at = {box[0], box[1] + 13.0f, box[2], 1.0f};

    g_fx->item_full = 1.0f;

    if (big) { // as BtGetTreasureboxBig_Loop lays the weapon by its owner
        int user = ((CDngStatusData *) UserStatus)->CheckWeaponUser(item_no);

        at[0] -= 5.0f;

        if (user == CHARA_XIAO) {
            g_fx->item->SetRotation(-HALF_PI, -HALF_PI, 0.0f);
            at[0] += 3.5f;
        } else if (user == CHARA_OSMOND) {
            g_fx->item->SetRotation(-HALF_PI, PI_SHORT, 0.0f);
            at[0] += 2.5f;
        } else if (user == CHARA_RUBY) {
            g_fx->item->SetRotation(0.0f, HALF_PI_SHORT, 0.0f);
            at[0] += 3.5f;
            g_fx->item_full = 2.0f;
        } else {
            g_fx->item->SetRotation(HALF_PI, 0.0f, HALF_PI);
        }
    }

    g_fx->item->SetPosition(at);
    g_fx->item->SetScale(0.01f, 0.01f, 0.01f);
    g_fx->item_bob = -PI;
    std::printf("coop: partner chest item %s, %d quadwords\n", model_path, g_fx->item_arena.used);
}

void FreeOpeningItem() {
    if (g_fx != nullptr && g_fx->item != nullptr) {
        g_fx->item = nullptr;
        g_fx->item_drawn = 0.0f;
        TexManager.DeleteTextureBlock(kItemBlock);
        TexManager.CleanUpTextureList();
    }
}

// The partner stands in front of the box and plays its opening, still until it is done (StepBusy).
// coop-opt7 (local): her presentation is her copy's engine (its camera, jingle and messages); the world half poses her, and
// the other copies draw the glow and item (draw only; others).
void StartOpening(const float *box, bool big, bool local = false, bool others = false) {
    if (Mutated("chestturn")) {
        float dx = box[0] - g_partner.pos[0];
        float dz = box[2] - g_partner.pos[2];

        if (dx != 0.0f || dz != 0.0f) {
            g_partner.chara->frame->SetRotation(0.0f, atan2f(dx, dz), 0.0f);
        }
    } else {
        g_partner.pos[0] = box[0];
        g_partner.pos[2] = box[2] + kOpenFront;
        g_partner.chara->frame->SetRotation(0.0f, 0.0f, 0.0f);
        g_partner.chara->frame->SetPosition(box[0], g_partner.pos[1], box[2]);
    }

    int motion = big ? kOpenBig : kOpenSmall;

    g_partner.busy = 30;

    int frames = LoadChestMotions() ? ChestMotionFrames(motion) : 0;

    if (frames > 0) {
        PlayMotion(motion, ChestPlayFlags(2));
        g_partner.busy = std::min(kOpenTicksMax, std::max(30, frames));
    } else {
        PlayMotion(0, 0);
    }

    // coop-atlaopen: as long as retail's, then held on the item (StepOpening).
    if (!Mutated("chestquick")) {
        g_partner.busy = 1;
        g_partner.open_tick = 0;
        g_partner.open_big = big;
        g_partner.open_hold = false;
        if (!local) {
            PartnerSpecialLoad(2);
        }
    }

    if (!local || others) {
        StartOpeningFx(box, big); // coop-openercam (coop-opt7: the others' glow; its camera is never shown there)
    }
    std::fprintf(stderr, "coop: partner opening starts at box (%.1f %.1f %.1f), she stands (%.1f %.1f)\n", box[0], box[1], box[2],
                 g_partner.pos[0], g_partner.pos[2]); // coop-openpos: x-atla-open's chest spot
    sceVu0CopyVector(g_pose.box, (float *) box);
    g_pose.ticks = g_partner.open_tick >= 0 ? kOpenHold : g_partner.busy;
    g_pose.sampled = false;
    g_pose.after = 0;
}

// A chest the partner opens itself, in the world: the lid opens, the item goes to the shared
// inventory as the presentation's last step puts it there, and the item's message shows for a few
// seconds without holding anyone. Nothing of the player's is moved, posed or paused. A small chest
// gives an attachment, the map or the magic crystal; a large one a weapon. One the party has no room
// for (or a weapon its owner already has ten of) says so and stays shut.
void OpenBox(int active, int entry_no, bool big) {
    MessageBy       by(2); // coop-itemprompt: its messages are hers
    CDngStatusData *status = (CDngStatusData *) UserStatus;
    TREASURE_BOX   &box = NowDngMap->boxes[entry_no];
    int             item_no = box.item_no;
    int             volume = 0;

    if (big) {
        if (status->CheckWeaponRot(item_no) >= 10) {
            SndSePlay(SE_CHEST_OPEN_BIG, -1, 0);
            SetSystemMes(status->CheckWeaponUser(item_no) + 0x49, 0x96, MES_POS_BOTTOM, 0, NULL, NULL);
            DngMessMan.hide_timer = 0x96;
            return;
        }

        if (item_no == ITEM_WEAPON_STEVE && PlayerAllItemCheck(ITEM_WEAPON_STEVE) != 0) {
            item_no = ITEM_WEAPON_BONE_SLINGSHOT;
        }
    } else {
        int refusal = PartnerCheckItemGet(item_no); // coop-inventory: P2's own bag, or the party's

        if (refusal != 0) {
            SndSePlay(SE_CHEST_OPEN_SMALL, -1, 0);
            SetSystemMes(refusal == 1 ? 0x48 : 0x51, 0x96, MES_POS_BOTTOM, 0, NULL, NULL);
            DngMessMan.hide_timer = 0x96;
            return;
        }

        if (item_no >= ITEM_ATTACH_START && ITEM_NAME_TBL_NEW[item_no - ITEM_ATTACH_START] == NULL) {
            item_no = ITEM_ATTACH_PERIDOT;
        }

        volume = createAttachVolume(item_no, selectMapNo);
    }

    NowDngMap->events[active].kind = DNG_EVENT_NONE;
    box.lid_angle = -30.0f;
    box.closed = 0;

    if (Mutated("chestquick")) { // coop-atlaopen: else at their frames of her opening (StepOpening)
        SndSePlay(big ? SE_CHEST_OPEN_BIG : SE_CHEST_OPEN_SMALL, -1, 0);
        ItemGetMes(item_no, volume != 0 ? volume : -1, 0x96, 0);
    }

    DngMessMan.hide_timer = 0x96;

    if (!big && (unsigned int) (item_no - ITEM_MAP) < 2U) {
        if (item_no == ITEM_MAP) {
            BtEquipMap = 1;
        } else {
            BtEquipMasuisyou = 1;
        }
    } else if (big || !PartnerGetItem(item_no, -1, false)) { // coop-inventory: a bag item into P2's own bag
        status->GetItem(item_no, volume);
    }

    StartOpening(box.pos, big);
    g_partner.open_box = entry_no;
    g_partner.open_item = item_no;
    g_partner.open_volume = volume;
    LoadOpeningItem(box.pos, big, item_no);
    std::printf("coop: partner opens %s box %d: item %d x%d\n", big ? "large" : "small", entry_no, item_no, volume);
}

// coop-openercam: her Atla's presentation on her screen (Jep's 2026-10-27 playtest: "there is no opening animation for other
// characters, but ... still do the cinematic view and use the Atla moving animation on her screen even if her character stays
// stationary"). Retail's (BtAtraGetShort_Loop) plays the Atla effect (dun/effect/saget.chr) on the ball, its motion driving
// the camera (SetMotionCamera), until the effect's frame 129.5. Her copy of that effect: loaded, put on the ball and stepped
// on both copies alike while she stands still; drawn, and its camera shown, on her copy alone. Its own texture block (0x3E),
// so his presentation's (0x1C) is never touched. Heap memory, outside the snapshot's state set.
struct AtlaFx {
    u_int         *pack = nullptr;
    u_char        *model = nullptr;
    CDataAlloc2<1> arena;
    CCharacter    *chara = nullptr;
    int            ticks = 0; // ticks played, 0 when none
    bool           failed = false;
    float          lit = 0.0f; // the light's rate it was last drawn with on this copy (the test's line), 0 none
    bool           naming = false; // coop-closetitem: the effect over, her name's box up until her own press ends it
    int            board = 0;      // coop-atlacross: retail's message board rate (atraGetMsgBordRate), 4 a tick to 128
    int            fading = 0;      // coop-atlacross: ticks left of retail's fade after her press (steps 3-5), 0 none
    bool           entered = false; // coop-atlasync: the effect's pictures in this copy's texture table (gone after an apply)
    int            pictures = 0;        // coop-atlalight: how many it entered into its block ...
    char           picture[32] = {};    // ... and the first one's name, to tell they are still its own (AtlaOwnsBlock)
    int            said = -1;           // coop-atlalight: the drawn line's last word (-1 none yet this presentation)
};

constexpr int   kAtlaPackBytes = 1024 * 1024;
constexpr int   kAtlaQuads = 0x20000;
// coop-atlalight (Jep's 2026-11-04 pack 8 playtest: "the light effect coming out of the Atla looked badly distorted" as P2 took
// one while P1 fought): the effect's block was 0x3A, inside the game's item model cache (CMainItemModel::SetCashModel puts its
// slot N's pictures in block 0x38 + N, six slots: 0x38-0x3D), so player 1's third quick slot item (slot 2, loaded at every
// floor start and again as his menu closes) took the block's pictures away and put its own in, and the effect drew with an
// item's picture. Now 0x3E, which nothing on a floor uses (the lobby's sixth model's, in the title). DC_COOP_MUTATE=atlablock
// (twin): 0x3A, as before.
int AtlaBlock() {
    static const bool before = Mutated("atlablock");

    return before ? 0x3A : 0x3E;
}
constexpr int   kAtlaTicksMax = 400;
constexpr float kAtlaEnd = 129.5f; // BtAtraGetShort_Loop's step 1 ends there
constexpr int   kAtlaNameMes = 40; // AtraGetMes's system message
constexpr int   kAtlaNameTicks = 0x28; // coop-atlacross: retail's count for it (BtAtraGetShort_Loop step 1)
constexpr int   kAtlaFadeTicks = 17;   // coop-atlacross: its fade after the opener's press (steps 3-5: 128 / 8 + 1)
constexpr float kAtlaFront = 9.4f; // coop-openpos: where retail's Atla pose stands the body, in front of the ball (TakeAtla)

AtlaFx *g_atla = nullptr;
// coop-actorswap: the way she faces through her Atla, and the effect's turn toward her (radians about y), set by TakeAtla.
float g_atla_face = -PI;
float g_atla_end = -PI; // the way she faces as it ends (retail turns the player round: CharaMain.SetRotation(0, -PI, 0))
float g_atla_turn = 0.0f;
// coop-actorswap (Jep, 2026-10-31: "I don't want her to just jump on P1's screen, but I want the cinematic to look proper too"):
// closer to the ball than retail's pose stands Toan (kAtlaFront), she backs straight away from it to that distance over
// kAtlaSlideTicks of her presentation (a short step back on his screen, not a jump); from farther out she does not move.
constexpr int kAtlaSlideTicks = 10;
float g_atla_slide_from[2] = {};
float g_atla_slide_to[2] = {};
int   g_atla_slide = 0; // ticks of the slide left
int     g_atla_name = -1; // coop-joinfix: her Atla's id, its name still to show at the presentation's end; -1 none
// coop-atlaswap: the partner's Atla played with retail's own pose (Toan's 0x2E, then 0x2F), its frame on the ball (TakeAtla)
bool    g_atla_pose = false;
float   g_atla_pose_ball[2] = {};
constexpr int kAtlaPose = 0x2E;    // BtAtraGetShort_Loop's pose for Toan (his pack, c01d_ex00.chr)
constexpr int kAtlaPoseEnd = 0x2F; // ... and the one from the effect's end (frame 129.5)
constexpr int kAtlaEndTick = 130;   // coop-opt7: that frame's tick of her presentation (coop_opener.cpp's kAtlaNameTick)

// coop-atlalight: how many pictures the texture table holds in the effect's block, and the first one's name. (Its name, not its
// pixels: another block's load compacts the staging buffer, CleanUpBuffer, and moves them.)
int AtlaBlockPictures(const char **first) {
    int count = 0;

    for (const CTexture &texture : TexManager.textures) {
        if (texture.block != AtlaBlock()) {
            continue;
        }
        if (count++ == 0 && first != nullptr) {
            *first = texture.name;
        }
    }

    return count;
}

// coop-atlalight: the block still holds the pictures the effect entered (not gone with a floor's start, nor another's).
bool AtlaOwnsBlock() {
    const char *first = nullptr;

    return g_atla != nullptr && g_atla->pictures > 0 && AtlaBlockPictures(&first) == g_atla->pictures && first != nullptr &&
           std::strncmp(first, g_atla->picture, sizeof g_atla->picture) == 0;
}

bool LoadAtlaFx() {
    if (g_atla == nullptr) {
        g_atla = new AtlaFx();
    }

    if (g_atla->failed) {
        return false;
    }

    // coop-atlalight: entered once, its pictures can still have gone without an apply: a floor's start (a new floor, a zone
    // reset, a resync's load) makes the whole texture table again (TexManager.Initialize) and loads the quick slots' items, and
    // the pack 8 log's second Atla played after two such loads with nothing entered (her effect drew with whatever the block
    // held). They are checked, and entered again when they are not its own. DC_COOP_MUTATE=atla_owned_off (twin): not checked,
    // as before.
    if (g_atla->entered) {
        if (AtlaOwnsBlock() || Mutated("atla_owned_off")) {
            return true;
        }
        std::printf("coop: partner Atla effect's pictures gone from block 0x%x (%d there): entered again\n", AtlaBlock(), AtlaBlockPictures(nullptr));
        g_atla->entered = false;
    }

    // coop-atlasync: loaded before, its pictures gone (AtlaOwnsBlock above): entered again, as the partner's
    // load does (coop_party.cpp Load), into the same buffers.
    const bool again = g_atla->chara != nullptr;

    if (!again) {
        g_atla->pack = reinterpret_cast<u_int *>(new (std::align_val_t{64}) u_char[kAtlaPackBytes]);
        g_atla->model = new (std::align_val_t{64}) u_char[static_cast<size_t>(kAtlaQuads) * 16];
    }

    int size = 0;

    if (LoadFile((char *) "dun/effect/saget.chr", g_atla->pack, &size) == 0 || size <= 0 || size > kAtlaPackBytes) {
        std::fprintf(stderr, "coop: cannot load dun/effect/saget.chr for her Atla\n");
        g_atla->failed = true;
        return false;
    }

    g_atla->arena.base = g_atla->model;
    g_atla->arena.buffer = g_atla->model;
    g_atla->arena.used = 0;
    g_atla->arena.limit = kAtlaQuads;
    if (again) {
        TexManager.DeleteTextureBlock(AtlaBlock());
        TexManager.CleanUpTextureList();
        g_atla->chara->Initialize();
    } else {
        g_atla->chara = new CCharacter();
    }
    g_atla->chara->LoadPackData2(g_atla->pack, (char *) "info.cfg", &g_atla->arena, AtlaBlock(), &g_atla->arena, 0);
    g_atla->entered = true;
    const char *first = nullptr;

    g_atla->pictures = AtlaBlockPictures(&first);
    std::snprintf(g_atla->picture, sizeof g_atla->picture, "%s", first != nullptr ? first : "");
    std::printf("coop: partner Atla effect %s, %d quadwords\n", again ? "entered again" : "loaded", g_atla->arena.used);
    return true;
}

// Her Atla's name (g_atla_name), as retail's presentation shows it at its end.
void ShowAtlaName() {
    if (g_atla_name < 0) {
        return;
    }
    {
        MessageBy by(2); // coop-itemprompt: on her screen only
        // coop-atlacross: retail's own count (BtAtraGetShort_Loop step 1: AtraGetMes(..., 0x28)); DC_COOP_MUTATE=atlaname_hang
        // (twin): 0x96, the presentation's end left to the message's own wait, as coop-closetitem had it.
        AtraGetMes(selectMapNo, g_atla_name, Mutated("atlaname_hang") ? 0x96 : kAtlaNameTicks);
    }

    // coop-closetitem (Jep, 2026-10-31: "for xiao it should wait for x", the X that ends the presentation): the name stays up
    // until her own press ends the presentation, as retail's does for its opener (StepAtlaFx), and his presses never take it.
    // (coop-itemprompt had it go in its time instead, when the wait read player 1's pad.) DC_COOP_MUTATE=atlaname_timeout
    // (twin): it goes in its time, nothing held.
    if (Mutated("atlaname_timeout")) {
        SystemMesInputKey = 0;
    }

    DngMessMan.hide_timer = 0x96;
    std::printf("coop: partner's Atla name shown (id %d) at effect frame %.1f\n", g_atla_name,
                g_atla != nullptr && g_atla->chara != nullptr ? g_atla->chara->motion_type.state.time : -1.0f);
    g_atla_name = -1;
}

// Her pose as her Atla starts (every copy): Toan's retail pose, her chest set's standing grab, or idle.
void AtlaPoseStart() {
    // coop-actorswap (Jep, 2026-10-30: "play the animation ... when they're getting an item and they're grabbing it in the air,
    // usually when opening a chest ... not an animation where she kneels first"): retail's Atla pose (0x2E/0x2F) is in Toan's
    // pack alone, so she stood idle; she plays her own chest set's standing grab instead (kAtlaGrab), from its first frame.
    // DC_COOP_MUTATE=atlagrab: idle, as before (twin).
    if (g_atla_pose) { // coop-atlaswap: Toan's own, as retail's
        PlayMotion(kAtlaPose, ChestPlayFlags(2));
    } else if (!Mutated("atlagrab") && LoadChestMotions() && ChestMotionFrames(kAtlaGrab) > 0) {
        PlayMotion(kAtlaGrab, ChestPlayFlags(2));
    } else {
        PlayMotion(0, 0);
    }
}

// As BtAtraGetShort_Loop's step 1: the effect on the ball, its motion from the start.
// coop-opt7 (others): her own presentation is her copy's engine; the other copies draw its effect alone (no chime or pose: her
// pose is the world's, AtlaPoseStart). coop-opt7 step 4: no copy gives it a camera any more (coop-atlaopen's camera and zoom on
// her copy, coop-openercam: her copy shows retail's own; unlinked, the one screen is player 1's, which never took it).
void StartAtlaFx(const float *ball, bool others = false) {
    // (x-dungeon-p2 p2atla's camtwin, DC_COOP_MUTATE=atlacam, takes hers just before his: plain, so the twin's frames of his
    // presentation are not her own Atla's.)
    if (Mutated("atlacam") || !LoadAtlaFx()) {
        return;
    }

    sceVu0FVECTOR at = {ball[0], ball[1], ball[2], 1.0f};

    g_atla->chara->SetPosition(at);
    g_atla->chara->SetRotation(0.0f, g_atla_turn, 0.0f);
    g_atla->chara->SetMotion(0, 6);
    // (coop-atlaopen: the effect's motion carries a camera's zoom, MOTION_KEY_CAMERA_FOV, which sets the game's one projection:
    // with no camera it never moves anyone's view)
    g_atla->chara->SetMotionCamera(nullptr);
    g_atla->ticks = 1;
    g_atla->naming = false; // coop-closetitem
    g_atla->said = -1;      // coop-atlalight
    if (others) {
        std::printf("coop: partner Atla effect drawn on this copy (her presentation is her copy's)\n");
        return;
    }
    PartnerSpecialLoad(1); // coop-atlaopen: her Atla's chime
    AtlaPoseStart();
    std::printf("coop: partner Atla presentation starts at (%.1f %.1f %.1f), she stands (%.1f %.1f) facing %.3f, effect turned %.3f, steps to (%.1f %.1f)\n",
                ball[0], ball[1], ball[2], g_partner.pos[0], g_partner.pos[2], g_atla_face, g_atla_turn,
                g_atla_slide > 0 ? g_atla_slide_to[0] : g_partner.pos[0], g_atla_slide > 0 ? g_atla_slide_to[1] : g_partner.pos[2]);
}

// Each tick of it: the effect's next frame; true while it plays (she stands still).
bool StepAtlaFx() {
    if (g_atla == nullptr || g_atla->ticks == 0) {
        return false;
    }

    if (g_atla->naming) {
        // coop-atlacross (the actor swap thread, 2026-10-31: x-atla-open's Cross 60 ticks after her name ended nothing): the
        // effect over, retail's presentation fades its message board in (step 2: 4 a tick to 128, 32 ticks), then waits for its
        // opener's Circle or Cross (step 3: GamePad.Down(PAD_CIRCLE | PAD_CROSS)) and clears the name as it ends (step 5,
        // ClearSystemMes). Hers did the same only through the name's own wait: Cross alone, and only once its 0x96 count ran
        // down, so a press in its first 2.5 s did nothing. Now her own press on the agreed pads (NetplayPlayerPressed), as
        // retail's steps. The link gone, it ends.
        // DC_COOP_MUTATE=atlaname_hang (twin): held until the name's own wait lets it go, as before.
        const bool hang = Mutated("atlaname_hang");
        const bool name_up = SystemMesNo == kAtlaNameMes && SystemMesInputKey != 0;

        // coop-atlaswap (Jep's 2026-11-02 playtest: "the cutscene has a weird zoom at the end"): retail steps its effect every tick
        // while atraGetStatus is set (motiondrive.cpp), its name's steps 2-4 too, so the effect's camera keys put its close-up and
        // its field of view (376.9) back each tick. Hers stopped at the effect's end, and the name's 3D icon (item_icon3d.cpp,
        // MenuWorldTrans) left the game's projection at 800, which then stood on her close-up camera: her screen zoomed in on it
        // until her press (x-opener-view -Swap: projection 800 from the effect's end, retail's 376.9 to its fade).
        g_atla->chara->Step();

        if (!hang && g_atla->board < 128) {
            g_atla->board += 4;
            return true;
        }
        // (her press is the presentation's: retail's fade out and in after it (steps 3-5: 8 a tick to 128, then its last step)
        // holds the opener kAtlaFadeTicks more, so the press, and its release, never reach her walk, where Circle locks on and
        // turns her as it is let go)
        if (!hang && g_atla->fading == 0) {
            if (NetplayLinkUp() && !NetplayPlayerPressed(2, kInputCross | kInputCircle)) {
                return true;
            }
            g_atla->fading = kAtlaFadeTicks;
            if (name_up) {
                ClearSystemMes();
            }
            return true;
        }
        if (!hang && --g_atla->fading > 0 && NetplayLinkUp()) {
            return true;
        }
        if (hang && NetplayLinkUp() && name_up) {
            return true;
        }
        g_atla->naming = false;
        g_atla->fading = 0;
        std::fprintf(stderr, "coop: partner's Atla presentation over at link tick %lld: her name's box gone\n", (long long) NetplayLinkTick());
    } else {
        float was = g_atla->chara->motion_type.state.time;

        g_atla->chara->Step();

        if (g_atla_slide > 0) { // coop-actorswap: her step back to retail's distance
            const float t = (float) (kAtlaSlideTicks - --g_atla_slide) / (float) kAtlaSlideTicks;

            g_partner.pos[0] = g_atla_slide_from[0] + (g_atla_slide_to[0] - g_atla_slide_from[0]) * t;
            g_partner.pos[2] = g_atla_slide_from[1] + (g_atla_slide_to[1] - g_atla_slide_from[1]) * t;
            g_partner.chara->frame->SetPosition(g_partner.pos[0], g_partner.pos[1], g_partner.pos[2]);
            if (g_atla_slide == 0) {
                std::fprintf(stderr, "coop: partner stepped back to (%.1f %.1f) for her Atla\n", g_partner.pos[0], g_partner.pos[2]);
            }
        }

        // coop-actorswap (DC_COOP_DEBUG): where the effect's light and camera stand against her and the ball, mid-presentation.
        if (g_atla->ticks == 136 && CoopDebug()) {
            CFrame       *lamp = g_atla->chara->frame != nullptr ? g_atla->chara->frame->SearchFrame((char *) "light01") : nullptr;
            sceVu0FVECTOR up = {0.0f, 0.0f, 0.0f, 1.0f};
            sceVu0FVECTOR world = {0.0f, 0.0f, 0.0f, 1.0f};

            if (lamp != nullptr) {
                up[0] = lamp->local[3][0];
                up[1] = lamp->local[3][1];
                up[2] = lamp->local[3][2];
                lamp->GetWorldPosition(world, up);
            }
            std::fprintf(stderr, "coop-debug: atla geometry: effect at (%.1f %.1f) turned %.3f; light (%.1f %.1f); her (%.1f %.1f) facing %.3f\n",
                         g_atla->chara->pos[0], g_atla->chara->pos[2], g_atla_turn, world[0], world[2], g_partner.pos[0], g_partner.pos[2], g_atla_face);
        }

        // coop-actorswap: where her grab stands a few ticks in (r-chest-motion reads it).
        if (g_atla->ticks == 6) {
            float now = 0.0f;
            int   start = 0, end = 0;

            if (g_partner.motion == kAtlaGrab && ChestMotionAt(kAtlaGrab, &now, &start, &end)) {
                std::fprintf(stderr, "coop: partner Atla grab motion %d at frame %.1f, 6 ticks in (frames %d to %d)\n", kAtlaGrab, now, start, end);
            } else {
                std::fprintf(stderr, "coop: partner Atla grab none: motion %d, 6 ticks in\n", g_partner.motion);
            }
        }

        // coop-atlaopen: its chime (special set 1's sound 1) as the effect passes frame 53, as BtAtraGetShort_Loop plays it.
        if (was < 53.0f && g_atla->chara->motion_type.state.time >= 53.0f) {
            PartnerSpecialSound(1, 1);
        }

        if (g_atla->chara->motion_type.state.time < kAtlaEnd && ++g_atla->ticks <= kAtlaTicksMax) {
            return true;
        }

        std::fprintf(stderr, "coop: partner Atla presentation ends after %d ticks (effect frame %.1f)\n", g_atla->ticks,
                    g_atla->chara->motion_type.state.time);
        ShowAtlaName(); // coop-joinfix (before the ticks go: its line says the effect frame it ended at)

        if (g_atla_pose) { // coop-atlaswap: Toan's pose from the effect's end, as retail's step 1 sets it (motion_no 0x2F)
            PlayMotion(kAtlaPoseEnd, ChestPlayFlags(0));
        }

        if (SystemMesNo == kAtlaNameMes && SystemMesInputKey != 0) {
            g_atla->naming = true; // coop-closetitem: held till her press ends it
            g_atla->board = 0;
            g_atla->fading = 0;
            return true;
        }
    }

    g_atla->ticks = 0;

    if (g_partner.motion == kAtlaGrab || g_atla_pose) { // coop-actorswap: her grab ends with the presentation (Toan's pose too)
        PlayMotion(0, 0);
    }
    g_atla_pose = false; // coop-atlaswap: her frame back on her place (PartnerFramePlace), where the pose stood her body

    // coop-atlaopen: as retail's presentation ends, she turns round (CharaMain.SetRotation(0, -PI, 0)); coop-actorswap: round
    // from the way the effect was turned to her, so toward the ball (g_atla_end).
    if (!Mutated("atlaside")) {
        g_partner.chara->frame->SetRotation(0.0f, g_atla_end, 0.0f);
        g_partner.heading[0] = sinf(g_atla_end);
        g_partner.heading[1] = 0.0f;
        g_partner.heading[2] = cosf(g_atla_end);
    }

    return false;
}

// coop-atlaopen: retail lights the opener from its Atla through the presentation (Draw_MainUnit, while atraGetStatus): a
// point light at the effect's "light01" frame, brightening as the effect rises (atraGetStatusRate, 2 a tick to 256). Her
// body is drawn in it on the copy that shows her Atla. False when there is none to draw her in.
// DC_COOP_MUTATE=atlalight: none, as before.
bool AtlaLight(const float *stood, sceVu0FMATRIX light, sceVu0FMATRIX colour) {
    if (g_atla != nullptr) {
        g_atla->lit = 0.0f;
    }

    if (g_atla == nullptr || g_atla->ticks == 0 || (!DrawsPartnerPresentation() && Mutated("atlafx_hers")) || Mutated("atlalight")) {
        return false;
    }

    CFrame *lamp = g_atla->chara->frame != nullptr ? g_atla->chara->frame->SearchFrame((char *) "light01") : nullptr;

    if (lamp == nullptr) {
        return false;
    }

    sceVu0FVECTOR up = {lamp->local[3][0], lamp->local[3][1], lamp->local[3][2], 1.0f};
    sceVu0FVECTOR world;
    sceVu0FVECTOR towards;

    lamp->GetWorldPosition(world, up);
    towards[0] = world[0] - stood[0];
    towards[1] = world[1] - stood[1] - 12.0f;
    towards[2] = world[2] - stood[2];
    towards[3] = 0.0f;

    float dist = sqrtf(towards[0] * towards[0] + towards[1] * towards[1] + towards[2] * towards[2]);
    float power = 5.0f * (100.0f / (std::max(dist, 10.0f) * std::max(dist, 10.0f)));
    float rate = std::min(256.0f, 2.0f * (float) g_atla->ticks);

    sceVu0Normalize(towards, towards);
    light[0][3] = towards[0] * power;
    light[1][3] = towards[1] * power;
    light[2][3] = towards[2] * power;
    light[3][3] = 0.0f;
    colour[3][0] = 0.8359375f * rate;
    colour[3][1] = 0.9765625f * rate;
    colour[3][2] = 0.66015625f * rate;
    colour[3][3] = 0.5f * rate;
    g_atla->lit = rate;
    return true;
}

// coop-actorswap: the stance of a member with no Atla pose of her own, from where she stood at the ball (Jep's 2026-10-30/31
// rules, TakeAtla below): the effect turned to her (turn), she facing that way (face) and round to the ball as it ends (end);
// closer than retail's kAtlaFront, a step straight back to it (slides, to). coop-atlaswap: shared by her partner's Atla and the
// game's player's own (PlayerAtlaStance).
struct AtlaStance {
    float turn = 0.0f;
    float face = -PI;
    float end = -PI;
    bool  slides = false;
    float to[2] = {};
};

AtlaStance StanceAt(const float *ball, const float *stood) {
    AtlaStance stance;
    float      dx = stood[0] - ball[0];
    float      dz = stood[2] - ball[2];
    float      d = sqrtf(dx * dx + dz * dz);

    if (d < 0.01f) { // on the ball itself: retail's front (+z)
        dx = 0.0f;
        dz = 1.0f;
        d = 0.0f;
    }

    const float unit = d > 0.0f ? d : 1.0f;

    stance.turn = atan2f(dx, dz);
    stance.face = Mutated("atlafaceball") ? atan2f(-dx, -dz) : stance.turn; // (twin: toward the ball, as before)
    stance.end = atan2f(-dx, -dz);
    stance.slides = d < kAtlaFront && !Mutated("atlastay");
    stance.to[0] = ball[0] + dx / unit * kAtlaFront;
    stance.to[1] = ball[2] + dz / unit * kAtlaFront;
    return stance;
}

// coop-atlaswap: the game's player's own Atla under her stance (PlayerAtlaStance): its end turn and its step back. In the state
// set (g_atla_*), as hers.
bool  g_atla_player = false;
float g_atla_player_end = -PI;
int   g_atla_player_slide = 0;
float g_atla_player_from[2] = {};
float g_atla_player_to[2] = {};
// coop-atlaswap: the member whose pose pack the player's Atla Init loaded (PlayerAtlaPack), -1 none asked
int   g_atla_player_pack = -1;
// coop-atlahold: her grab's frame at the last tick, and whether it has been seen under way (not the motion before it)
float g_atla_player_time = 0.0f;
bool  g_atla_player_seen = false;

// The stance her Atla stands her in (TakeAtla's rules below; coop-opt7: her world half's too, PartnerPresentAtla): her place,
// the way she faces, the effect's turn toward her and her step back, or Toan's retail pose (g_atla_pose). Every copy alike.
void AtlaStanceAt(const float *ball) {
    g_atla_turn = 0.0f;
    g_atla_face = -PI;
    g_atla_end = -PI;
    g_atla_pose = false;
    if (!Mutated("atlaside")) {
        bool centre = Mutated("atlacentre");
        bool jump = centre || Mutated("atlajump");

        if (jump) {
            g_partner.pos[0] = ball[0];
            g_partner.pos[2] = ball[2] + (centre ? 0.0f : kAtlaFront);
            g_partner.chara->frame->SetPosition(g_partner.pos[0], g_partner.pos[1], g_partner.pos[2]);
            g_atla_face = centre ? 0.0f : -PI;
        } else {
            const AtlaStance stance = StanceAt(ball, g_partner.pos);

            g_atla_turn = stance.turn;
            g_atla_face = stance.face;
            g_atla_end = stance.end;

            g_atla_slide = 0;
            // coop-atlaswap (Jep's 2026-11-02 playtest: "if P2 touches atla as toan he stands in the wrong spot in the
            // cutscene"): after a swap her member is Toan, whose chest set holds retail's Atla pose (0x2E: frame on the ball, his
            // body carried 9.4 out, facing it, reaching up). Her stance's chest grab (kAtlaGrab, the big chest's opening) carried
            // his body some 10 further out from where he stood, to the effect camera's edge (x-opener-view -Swap -Who 2: Toan cut
            // off in the corner, retail's in frame). Toan plays retail's pose, its frame on the ball turned to where he stood, so
            // the pose stands his body 9.4 out on his side, as retail's on its +z; his place is there through it (no step back).
            // DC_COOP_MUTATE=atlaswap_toangrab: the grab, as before (the twin).
            g_atla_pose = g_partner.loaded_chara == CHARA_TOAN && !Mutated("atlaswap_toangrab") && LoadChestMotions() &&
                          ChestMotionFrames(kAtlaPose) > 0;
            if (g_atla_pose) {
                g_atla_pose_ball[0] = ball[0];
                g_atla_pose_ball[1] = ball[2];
                g_partner.pos[0] = stance.to[0];
                g_partner.pos[2] = stance.to[1];
            } else if (stance.slides) {
                g_atla_slide_from[0] = g_partner.pos[0];
                g_atla_slide_from[1] = g_partner.pos[2];
                g_atla_slide_to[0] = stance.to[0];
                g_atla_slide_to[1] = stance.to[1];
                g_atla_slide = kAtlaSlideTicks;
            }
        }

        g_partner.chara->frame->SetRotation(0.0f, g_atla_face, 0.0f);
        g_partner.heading[0] = sinf(g_atla_face);
        g_partner.heading[1] = 0.0f;
        g_partner.heading[2] = cosf(g_atla_face);
    }
}

// coop-p2atla: what retail's Atla presentation does to the world when it starts (BtAtraGetShort_Loop's first
// step), without the presentation: the ball goes, the Atla is saved (getAtraToSaveData, which player 2's copy
// apart also queues for the host) and its name is shown. No task starts, so the player plays on and each copy
// keeps its own camera.
void TakeAtla(int active, int entry_no) {
    ATRA_BOLL &ball = NowDngMap->atra[entry_no];
    int        atra_id = UserStatus->atra_registry[selectMapNo][ball.atra_no].id;

    // (coop-actorswap, Jep's 2026-10-31 playtest: the floor's ball goes at once; the Atla rising in her presentation's effect, now
    // drawn on both screens, is the one that stays until she can move again.)
    ball.used = 0;
    NowDngMap->events[active].kind = DNG_EVENT_NONE;
    getAtraToSaveData(atra_id, ball.atra_no, SaveData, selectMapNo, UserStatus->cur_floor);

    // coop-atlaopen (Jep's 2026-10-27 playtest: "Xiao stands on the wrong side of the Atla so its light beam goes away from
    // her"): retail's presentation stands the opener on the ball, unturned (CharaMain.SetPosition(ball), SetRotation(0)),
    // and the effect's beams are laid out from there; she stood wherever her Cross found the ball. Both copies alike.
    // DC_COOP_MUTATE=atlaside: where she stood, as before. (Her chime, StepAtlaFx, replaces the SE_ITEM_GET played here,
    // which retail's presentation never plays.)
    // coop-openpos (Jep's 2026-10-28 playtest: her place at the Atla "still off"): retail's frame is on the ball, but its pose
    // (motion 0x2E, only in Toan's pack) carries his body out 9.4 to the ball's front (+z), facing it and reaching up to it
    // (x-opener-view atla -Who 1, his root bone through it). Hers has no such pose, so her frame on the ball left her idle in
    // its middle, facing away. She stands where his body does: kAtlaFront in front, turned to it, as retail's ends turned.
    // DC_COOP_MUTATE=atlacentre: on the ball, unturned (coop-atlaopen's place).
    // coop-actorswap (Jep's 2026-10-30 playtest, comparing hers with Toan's: she should be "relatively close to where he's
    // positioned and looking towards the atla", and "from the other player's perspective she doesn't just jump across the atla,
    // her position stays the same and the atla animation plays to her"): she stays where her Cross found the ball (inside its
    // reach, so near where retail's pose stands Toan), and the effect, whose rising Atla and light go out of its front (+z, the
    // side retail's pose stands the body on: its light01 ends some 37 out, "coop-debug: atla geometry"), is turned to her.
    // She faces the way the effect was turned, out toward its light, as retail's frame stays unturned (rotation 0, facing +z)
    // while its Atla rises in front of him (Jep's 2026-10-31 playtest: she faced the ball, away from it), and turns round to the
    // ball as it ends. DC_COOP_MUTATE=atlajump: kAtlaFront in front of the ball, unturned effect, as coop-openpos had it (twin).
    AtlaStanceAt(ball.pos);

    std::printf("coop: partner (%d) takes Atla %d: id %d, slot %d\n", g_partner.loaded_chara, entry_no, atra_id, ball.atra_no);
    StartAtlaFx(ball.pos); // coop-openercam

    // coop-joinfix (Jep's 2026-10-29 playtest, parked 3: her Atla's "acquired" name came at once, his about a second in): retail
    // shows the name as its presentation ends (BtAtraGetShort_Loop step 1, effect frame 129.5), so hers waits for her own
    // presentation's end too (StepAtlaFx); at once only when there is none to wait for. DC_COOP_MUTATE=atla_name_early (twin):
    // at the take, as before.
    g_atla_name = atra_id;
    if (g_atla == nullptr || g_atla->ticks == 0 || Mutated("atla_name_early")) {
        ShowAtlaName();
    }

}

// coop-p2hurt (Jep's 2026-10-28 playtest: no pickup animation for Dran's crest): her gate key's presentation, as retail's
// (BtGetGateKey_Init / _Loop) plays the player's but on her own: the pickup motion (0x22), then she holds the key up (0x23)
// with its message until her Cross or Circle, the key's model on her "item" frame and a camera 40 in front of her, 20 up,
// looking at her chest (her copy only, draw side). Nothing of the player's is held: no monster or effect freeze (she cannot
// be hit while it plays), no game task, his camera and pad untouched. The stages are simulation state (g_partner), alike
// on both copies; the model and camera are heap, her copy only. DC_COOP_MUTATE=keypres (twin): the key's message at once,
// no presentation, as before.
constexpr int kKeyPickMotion = 0x22;
constexpr int kKeyHoldMotion = 0x23;

struct KeyFx {
    u_int         *files = nullptr;
    u_char        *model = nullptr;
    CDataAlloc2<1> arena;
    CFrame        *item = nullptr;
    CCameraFollow *camera = nullptr;
};

KeyFx *g_key = nullptr;

void FreeKeyModel() {
    if (g_key != nullptr && g_key->item != nullptr) {
        g_key->item = nullptr;
        TexManager.DeleteTextureBlock(kItemBlock);
        TexManager.CleanUpTextureList();
    }
}

void LoadKeyFx(int item_no) {
    if (!DrawsPartnerPresentation()) {
        return;
    }

    if (g_key == nullptr) {
        g_key = new KeyFx;
        g_key->files = reinterpret_cast<u_int *>(new (std::align_val_t{64}) u_char[2 * kItemFileBytes]);
        g_key->model = new (std::align_val_t{64}) u_char[static_cast<size_t>(kItemQuads) * 16];
        g_key->camera = new CCameraFollow(20.0f, 15.0f, 0.0f, 8.0f);
    }

    // The camera, as BtGetGateKey_Loop's second step sets SubCamera.
    sceVu0FVECTOR rotation;
    sceVu0FVECTOR eye;
    sceVu0FVECTOR ref;

    g_partner.chara->frame->GetRotation(rotation);
    *g_key->camera = MainCamera__4;
    g_key->camera->FollowOff();
    sceVu0CopyVector(ref, g_partner.pos);
    ref[1] += 12.0f;
    g_key->camera->SetRef(ref);
    sceVu0CopyVector(eye, g_partner.pos);
    eye[0] += 40.0f * sinf(rotation[1]);
    eye[1] += 20.0f;
    eye[2] += 40.0f * cosf(rotation[1]);
    g_key->camera->SetPos(eye);
    g_key->camera->Step(-1);
    g_key->camera->FollowOff();

    char   model_path[64];
    char   texture_path[64];
    int    model_size = 0;
    int    texture_size = 0;
    u_int *texture = reinterpret_cast<u_int *>(reinterpret_cast<u_char *>(g_key->files) + kItemFileBytes);

    FreeKeyModel();
    BtGetItemNamePath(model_path, texture_path, item_no);

    if (LoadFile(model_path, g_key->files, &model_size) == 0 || model_size <= 0 || model_size > kItemFileBytes ||
        LoadFile(texture_path, texture, &texture_size) == 0 || texture_size <= 0 || texture_size > kItemFileBytes) {
        std::fprintf(stderr, "coop: cannot load %s for her gate key\n", model_path);
        return;
    }

    g_key->arena.base = g_key->model;
    g_key->arena.buffer = g_key->model;
    g_key->arena.used = 0;
    g_key->arena.limit = kItemQuads;
    SetTempTexture(kItemBlock, (char *) texture);
    g_key->item = LoadMDSFile(g_key->files, &g_key->arena, 0, NULL, NULL);

    if (g_key->item == nullptr) {
        return;
    }

    CFrame *hand = g_partner.chara->frame->SearchFrame((char *) "item");
    sceVu0FVECTOR origin = {0.0f, 0.0f, 0.0f, 1.0f};

    if (hand != nullptr) {
        g_key->item->SetReference(hand);
    }

    g_key->item->SetPosition(origin);
    g_key->item->SetRotation(0.0f, 0.0f, 0.0f);
    g_key->item->SetScale(1.0f, 1.0f, 1.0f);
    std::fprintf(stderr, "coop-keypres: her gate key model %s on %s\n", model_path, hand != nullptr ? "her item frame" : "her body (no item frame)");
}

void EndKeyPresentation(const char *why) {
    if (g_partner.key_message > 0 && SystemMesNo == g_partner.key_message) {
        ClearSystemMes();
    }

    if (g_partner.motion == kKeyPickMotion || g_partner.motion == kKeyHoldMotion) {
        PlayMotion(0, 0);
    }

    std::fprintf(stderr, "coop-keypres: her gate key %d presentation ends (%s)\n", g_partner.key_item, why);
    g_partner.key_stage = 0;
    g_partner.key_item = -1;
    g_partner.key_message = -1;
    FreeKeyModel();
}

// Her message for the key, up until she clears it, and the key held up.
void HoldKeyUp() {
    MessageBy by(2); // coop-itemprompt: on her screen only

    ItemGetMes(g_partner.key_item, -1, -1, 0);
    g_partner.key_message = SystemMesNo;
    g_partner.key_stage = 2;

    if (HasMotion(kKeyHoldMotion)) {
        PlayMotion(kKeyHoldMotion, 0);
    }

    std::fprintf(stderr, "coop-keypres: she holds up gate key %d (motion %d)\n", g_partner.key_item, g_partner.motion);
}

// True when the presentation started (her pickup has its message from it).
bool StartKeyPresentation(int item_no) {
    if (Mutated("keypres") || g_partner.chara == nullptr || g_partner.down > 0) {
        return false;
    }

    g_partner.key_item = item_no;
    g_partner.key_stage = 1;
    g_partner.key_message = -1;
    g_partner.velocity[0] = g_partner.velocity[2] = 0.0f;
    g_partner.attacking = false;
    LoadKeyFx(item_no);
    std::fprintf(stderr, "coop-keypres: her gate key %d presentation starts\n", item_no);

    if (!HasMotion(kKeyPickMotion)) {
        HoldKeyUp();
        return true;
    }

    PlayMotion(kKeyPickMotion, 2);
    g_partner.chara->motion_type.state.time = (float) g_partner.chara->motion_type.motion_info[kKeyPickMotion].start;
    return true;
}

// coop-chestface (q-chest-face-ungaga stood 20 from the box, -ruby faced it 0.31): the Cross that closes her presentation
// is the presentation's, as retail's (BtGetTreasurebox*_Loop takes GamePad.Down; the player's control starts the next
// tick). StepBusy let her walk on in the closing tick, so CombatInput took the same press as a swing: Ungaga's lunge
// carried her body 10 further out, Ruby's and Xiao's shots turned her. Held for that tick, the press goes nowhere else.
// DC_COOP_MUTATE=closepress (twin): she acts on it, as before.
bool ClosingPressHeld() {
    return !Mutated("closepress");
}

// One tick of it: true while it lasts, holding her still.
bool StepKeyPresentation(int pressed) {
    if (g_partner.key_stage == 0) {
        return false;
    }

    g_partner.invincible = std::max(g_partner.invincible, 2);

    if (g_partner.key_stage == 1) {
        const MOTION_INFO &info = g_partner.chara->motion_type.motion_info[kKeyPickMotion];
        float              time = g_partner.chara->motion_type.state.time;

        if (g_partner.motion != kKeyPickMotion) { // a swap or a fall took her motion
            EndKeyPresentation("her motion changed");
            return false;
        }

        if (time >= info.end - 0.5f && time <= info.end) {
            HoldKeyUp();
        }

        return true;
    }

    if ((pressed & (kInputCross | kInputCircle)) != 0 || (HasMotion(kKeyHoldMotion) && g_partner.motion != kKeyHoldMotion)) {
        const bool by_press = (pressed & (kInputCross | kInputCircle)) != 0;

        EndKeyPresentation(by_press ? "her Cross or Circle" : "her motion changed");
        return by_press && ClosingPressHeld();
    }

    return true;
}

} // namespace

// coop-atlaapart (Jep's 2026-10-28 Local playtest: "P2 cannot touch an Atla when P1 is not in the same zone"): retail's Cross
// takes an Atla for Toan alone (else NotGetAtraMes, "can't take it"). Together her partner takes one as any member (TakeAtla,
// coop-p2atla); apart, her copy plays her as the game's player, her member (Xiao), so the same rule refused her. On her copy
// apart her player takes it through retail's presentation, which leaves Toan's pose pack off a member that is not Toan
// (BtAtraGetShort_Loop). Her copy by its role: apart (or before its first link) the link is down and NetplayLocalPlayer is 0.
// Player 1's own Cross stays retail's. DC_COOP_MUTATE=atlaapart: Toan alone, as before (the twin).
// coop-atlaswap (Jep's 2026-11-02 playtest: "P1 can't touch atla as xiao"): since the actor swap either player plays any member,
// and linked the game's player is player 1's on both copies, Xiao after a swap. With co-op on, the game's player takes it as
// any member on every copy, as her partner does. DC_COOP_MUTATE=atlaswap_toan: Toan alone for player 1, as before (the twin).
bool OwnPlayerTakesAtla() {
    if (NetplayRole() == 2 && !NetplayLinkUp() && !NetplayStayerMerge()) {
        return !Mutated("atlaapart");
    }

    return NetplayConfigured() && !Mutated("atlaswap_toan");
}

// coop-atlaswap: the game's player's own Atla as a member who is not Toan (player 1 after a swap, player 2 on her copy apart).
// Retail's presentation (BtAtraGetShort_Loop) stands the player's frame on the ball, unturned, and Toan's pose (0x2E, his pack
// alone) carries his body out 9.4 in front of it, reaching up; any other member (coop-atlaapart) stood idle in the ball's
// middle, facing away, the look coop-openpos took off her partner. She now takes her partner's stance (StanceAt, TakeAtla):
// where she pressed Cross, facing out the way the effect is turned to her, her own chest set's standing grab (kAtlaGrab, the
// pack her chest presentation loads), a step back to 9.4 when closer, round to the ball as it ends.
// DC_COOP_MUTATE=atlaswap_stand: she stands idle on the ball, as coop-atlaapart had it (the twin).
bool PlayerAtlaPosed() {
    const int chara = UserStatus != nullptr ? UserStatus->cur_chara : CHARA_TOAN;

    return chara != CHARA_TOAN && chara >= 0 && chara < 6 && NetplayConfigured() && !Mutated("atlaswap_stand");
}

// (coop-atlaswap: every Init that loads the player's Atla pose pack asks here, the port's override of BtAtraGetShort_Init too:
// another member's motions on her bones read wild matrices in motionDrive, the train-49 crash. Its line is the tests'.)
// DC_COOP_MUTATE=atlapack_retail: Toan's pack for every member, as that override had it (the twin).
const char *PlayerAtlaPack(const char *retail) {
    const char *pack = PlayerAtlaPosed() && !Mutated("atlapack_retail") ? kChestPacks[UserStatus->cur_chara] : retail;

    g_atla_player_pack = pack == retail ? CHARA_TOAN : UserStatus->cur_chara;
    std::fprintf(stderr, "coop: player's Atla pose pack %s (member %d)\n", pack, UserStatus != nullptr ? (int) UserStatus->cur_chara : -1);
    return pack;
}

// coop-atlaswap (the train-49 crash's guard): the pose pack loaded is the player's own member's, so its motions go on her bones.
// An Init that never asked PlayerAtlaPack loaded retail's, Toan's: only Toan's. Read by step 0 before it loads the pack.
bool PlayerAtlaPackFits() {
    const int chara = UserStatus != nullptr ? UserStatus->cur_chara : CHARA_TOAN;

    return (g_atla_player_pack < 0 ? CHARA_TOAN : g_atla_player_pack) == chara;
}

bool PlayerAtlaStance(const float *ball, const float *stood, float *turn) {
    g_atla_player = false;
    g_atla_player_slide = 0;

    if (!PlayerAtlaPosed()) {
        return false;
    }

    const AtlaStance stance = StanceAt(ball, stood);
    sceVu0FVECTOR    at = {stood[0], stood[1], stood[2], 1.0f};
    const bool       fits = PlayerAtlaPackFits();

    g_atla_player_pack = -1;
    CharaMain.SetPosition(at);
    CharaMain.SetRotation(0.0f, stance.face, 0.0f);
    BtActStatus.motion_no = fits ? kAtlaGrab : 0; // (another member's pack was not loaded onto her: she stands)
    if (!fits) {
        std::fprintf(stderr, "coop: player's Atla: the pose pack loaded is not member %d's: she stands without her grab\n", (int) UserStatus->cur_chara);
    }
    g_atla_player = true;
    g_atla_player_end = stance.end;
    g_atla_player_time = 0.0f;
    g_atla_player_seen = false;
    if (stance.slides) {
        g_atla_player_from[0] = stood[0];
        g_atla_player_from[1] = stood[2];
        g_atla_player_to[0] = stance.to[0];
        g_atla_player_to[1] = stance.to[1];
        g_atla_player_slide = kAtlaSlideTicks;
    }
    *turn = stance.turn;
    std::fprintf(stderr, "coop: player's Atla as member %d: stands (%.1f %.1f) facing %.3f, effect turned %.3f, steps to (%.1f %.1f)\n",
                 (int) UserStatus->cur_chara, stood[0], stood[2], stance.face, stance.turn, stance.slides ? stance.to[0] : stood[0],
                 stance.slides ? stance.to[1] : stood[2]);
    return true;
}

// coop-atlahold (Jep's 2026-11-04 pack 8 playtest: "xiaos animation is looping when grabbing the atla when P1 isnt in the zone"):
// her stance's grab (kAtlaGrab) was kept to the name's end, and the player's motion drive plays a motion round again at its end,
// so on the copy that plays her own Atla (hers apart, player 1's after a swap) the grab looped. Retail's big chest goes on to
// the held pose (0x2D) as the grab ends (BtGetTreasureboxBig, pose frame 79 of 80); hers now does the same, and holds it.
// Her partner's copy holds the grab's last frame (flag 2), no loop. DC_COOP_MUTATE=atlahold_loop: the grab kept (the twin).
void PlayerAtlaHold() {
    if (BtActStatus.motion_no != kAtlaGrab) {
        return;
    }

    int         index = -1;
    const auto *type = CharaMain.GetMotionParam(kAtlaGrab, &index, nullptr, nullptr, nullptr);
    const float start = type != nullptr && index >= 0 ? type->motion_info[index].start : 10.0f;
    const float end = type != nullptr && index >= 0 ? type->motion_info[index].end : 80.0f;
    const float now = CharaMain.GetNowTime();
    const bool  wrapped = g_atla_player_seen && now + 1.0f < g_atla_player_time;

    if (!g_atla_player_seen && now >= start && now < end - 1.0f) {
        g_atla_player_seen = true; // (the first ticks can still read the motion before it)
    }
    if (wrapped) {
        std::fprintf(stderr, "coop: player's Atla grab went round again (frame %.1f -> %.1f, frames %.0f to %.0f)\n", g_atla_player_time, now,
                     start, end);
    }
    g_atla_player_time = now;
    if (Mutated("atlahold_loop") || !g_atla_player_seen || (!wrapped && now < end - 1.0f)) {
        return;
    }

    BtActStatus.motion_no = kAtlaHold;
    std::fprintf(stderr, "coop: player's Atla grab ends at frame %.1f (frames %.0f to %.0f): member %d holds it up (motion %d)\n", now, start, end,
                 (int) UserStatus->cur_chara, kAtlaHold);
}

void PlayerAtlaStep() {
    if (!g_atla_player) {
        return;
    }

    PlayerAtlaHold();
    if (g_atla_player_slide <= 0) {
        return;
    }

    const float   t = (float) (kAtlaSlideTicks - --g_atla_player_slide) / (float) kAtlaSlideTicks;
    sceVu0FVECTOR at = {g_atla_player_from[0] + (g_atla_player_to[0] - g_atla_player_from[0]) * t, CharaMain.pos[1],
                        g_atla_player_from[1] + (g_atla_player_to[1] - g_atla_player_from[1]) * t, 1.0f};

    CharaMain.SetPosition(at);
}

float PlayerAtlaEndTurn() {
    if (!g_atla_player) {
        return -PI;
    }

    g_atla_player = false;
    return g_atla_player_end;
}

// coop-dungeonsuite (x-dungeon-suite small/map/crystal/atla -Who 1: player 2 stood frozen for the seconds his presentation
// ran, her stick ignored): his chest, Atla, gate key and attachment presentations are his alone.
bool PartnerPlaysThrough(int task) {
    if (Mutated("presfreeze")) {
        return false;
    }

    switch (task) {
        case GAME_TASK_BOX_BIG_LOOP:
        case GAME_TASK_BOX_SMALL_LOOP:
        case GAME_TASK_ATRA_GET_LOOP:
        case GAME_TASK_GATE_KEY_LOOP:
        case GAME_TASK_ATTACH_LOOP:
            return true;
    }

    return false;
}

// coop-p2hurt: her gate key's presentation camera while it plays, else null. (coop-opt7 step 4: her chest's and her Atla's
// cameras (coop-openercam) are gone: her copy shows retail's own, PresentEngineCamera.)
CCameraFollow *OpeningCamera() {
    if (g_key != nullptr && g_partner.key_stage > 0) {
        return g_key->camera;
    }
    return nullptr;
}

bool PartnerPresenting() {
    // (coop-opt7: her effect on the others' copies is draw only; the world's is her presentation's kind)
    return g_partner.busy > 0 || PresentKindOf(2) != kPresentNone || (!PresentLocalOn() && g_atla != nullptr && g_atla->ticks > 0) ||
           g_partner.key_stage > 0;
}

// Her Atla's effect, and her chest's (its glow, retail's itemOpenSmallFx / itemOpenBigFx, and the item she holds up), on her
// copy (CoopDraw).
void DrawPartnerPresentation() {
    // coop-actorswap (Jep's 2026-10-30 playtest: "Do the light effect too"): her Atla's effect is drawn on every copy, so player 1
    // sees it rise to her where she stands; only its camera and zoom stay her copy's (StartAtlaFx). DC_COOP_MUTATE=atlafx_hers:
    // her copy alone, as before (twin).
    if (g_atla != nullptr && g_atla->ticks > 0 && (DrawsPartnerPresentation() || !Mutated("atlafx_hers"))) {
        // coop-atlalight (x-opener-view -AtlaTex): whether it draws with its own pictures, said at its first draw and whenever
        // that changes.
        const int own = AtlaOwnsBlock() ? 1 : 0;

        if (own != g_atla->said) {
            std::fprintf(stderr, "coop: partner Atla effect drawn at tick %d from block 0x%x: its own pictures %s (%d there)\n", g_atla->ticks,
                         AtlaBlock(), own ? "yes" : "no", AtlaBlockPictures(nullptr));
            g_atla->said = own;
        }
        TexManager.ReloadTexture(Vif1Packet, AtlaBlock());
        g_atla->chara->Draw();
    }

    // coop-p2hurt: her gate key in her hand, as DrawDungeon draws the player's (gateItemFlag).
    if (DrawsPartnerPresentation() && g_key != nullptr && g_key->item != nullptr && g_partner.key_stage > 0) {
        TexManager.ReloadTexture(Vif1Packet, kItemBlock);
        MGDraw(g_key->item);
    }

    if (!ShowsPartnerChest()) { // coop-chestseen: her chest's on every copy
        return;
    }

    // coop-atlaopen: as DrawDungeon draws the player's (itemOpenSmallFlag, itemOpenBigFlag): the glow, then the item, grown
    // from the effect's frame 50 to 55 and bobbing.
    if (g_fx == nullptr || g_fx->playing < 0 || g_partner.busy == 0 || g_partner.open_tick < 0) {
        return;
    }

    CMotionModel *fx = g_fx->fx[g_fx->playing];
    bool          big = g_fx->playing == 1;

    g_fx->item_drawn = 0.0f;
    float         time = fx->motion.state.time;

    // coop-chestfix: the opening box at the effect's frame, where the floor's box stands (PartnerBoxDrawBegin left that out).
    g_fx->box_drawn = 0.0f;
    g_fx->glow_drawn = 0.0f;

    if (g_fx->hidden_box >= 0 && g_fx->box[g_fx->playing] != nullptr) {
        CMotionModel *box = g_fx->box[g_fx->playing];

        box->frame->SetPosition(NowDngMap->boxes[g_fx->hidden_box].pos);
        box->frame->SetRotation(0.0f, 0.0f, 0.0f);
        box->current_motion = 0;
        box->motion.state.time = time - 0.5f; // Step adds the motion's 0.5
        box->Step();
        box->Draw();
        g_fx->box_drawn = box->motion.state.time;
    }

    TexManager.ReloadTexture(Vif1Packet, 0x16);

    if (big) {
        MGDraw(fx->frame);
    } else {
        fx->Draw();
    }
    g_fx->glow_drawn = time;

    if (g_fx->item == nullptr || time <= 50.0f) {
        return;
    }

    float scale = g_fx->item_full * std::min(1.0f, 0.2f * (time - 50.0f));
    float lift[4];
    float held[4];

    g_fx->item->SetScale(scale, scale, scale);
    g_fx->item_bob += 0.10471976f;

    if (g_fx->item_bob >= PI) {
        g_fx->item_bob -= TWO_PI;
    }

    sceVu0CopyVector(held, g_fx->item->position);
    sceVu0CopyVector(lift, held);
    lift[1] += (big ? 0.03f : 0.5f) * sinf(g_fx->item_bob);
    g_fx->item->SetPosition(lift);
    TexManager.ReloadTexture(Vif1Packet, kItemBlock);
    MGDraw(g_fx->item);
    g_fx->item->SetPosition(held);
    g_fx->item_drawn = scale;
}

// coop-chestfix: on her copy through her chest's opening, the floor's box she opens is left out of the frame's DrawItemBox
// (its used flag off for the draw only), as retail's draw leaves the floor's boxes out for the opening box. Put back in
// PartnerBoxDrawEnd (CoopViewEnd), so the simulation never sees it. DC_COOP_MUTATE=chestlid (twin): the floor's box, lid open
// at once, as before.
void PartnerBoxDrawBegin() {
    if (g_fx == nullptr || g_fx->playing < 0 || g_partner.open_tick < 0 || g_partner.open_box < 0 || NowDngMap == nullptr ||
        !ShowsPartnerChest() || Mutated("chestlid") || g_fx->box[g_fx->playing] == nullptr) {
        return;
    }

    TREASURE_BOX &box = NowDngMap->boxes[g_partner.open_box];

    g_fx->hidden_box = g_partner.open_box;
    g_fx->hidden_used = box.used;
    box.used = 0;
}

void PartnerBoxDrawEnd() {
    if (g_fx == nullptr || g_fx->hidden_box < 0) {
        return;
    }

    if (NowDngMap != nullptr) {
        NowDngMap->boxes[g_fx->hidden_box].used = g_fx->hidden_used;
    }

    g_fx->hidden_box = -1;
}

// coop-b4story (Jep's LAN pack 7 playtest, 2026-11-01: B4's miniboss dropped its boxes, "P1 opened a chest and P2 couldn't see
// the other chest anymore"): through his chest's presentation (itemOpenSmallFlag, itemOpenBigFlag) retail's DrawDungeon draws his
// opening box alone and leaves every floor box out, and the presentation runs on both copies, so her screen lost all of them
// until his Cross. On her copy, from DrawDungeon's opening branch, the floor's boxes are drawn as in play from where she
// stands, all but the one he opens (the opening box's frame stands on it), that one left out for the draw only.
// DC_COOP_MUTATE=hisbox_hides (twin): every box left out on her screen, as before.
void HisPresentationBoxesDraw(const float *centre) {
    static int said = 0; // the presentation said for (1 small, 2 big), 0 none
    // (coop-warmconvert, WS7: said again at each job on a warm pair, as a fresh copy says it; trace_once_kept twin: kept)
    COOP_TEST_TRACE_RESET(said, 0);

    const int open = itemOpenSmallFlag != 0 ? 1 : itemOpenBigFlag != 0 ? 2 : 0;

    if (open == 0 || !DrawsPartnerPresentation() || NowDngMap == nullptr) {
        said = open == 0 ? 0 : said;
        return;
    }

    const bool          hides = Mutated("hisbox_hides");
    const CMotionModel &model = open == 1 ? itemOpenSmall : itemOpenBig;
    int                 his = -1;
    int                 others = 0;
    sceVu0FVECTOR       pos = {centre[0], centre[1], centre[2], 1.0f};

    for (int i = 0; i < 24; i++) {
        TREASURE_BOX &box = NowDngMap->boxes[i];

        if (box.used == 0) {
            continue;
        }
        if (his < 0 && model.frame != nullptr && std::hypot(box.pos[0] - model.frame->position[0], box.pos[2] - model.frame->position[2]) < 1.0f) {
            his = i;
        } else if (DistVector(box.pos, pos) <= 160.0f * NowDngMap->draw_dist_scale) {
            others++;
        }
    }

    if (!hides) {
        const int used = his >= 0 ? NowDngMap->boxes[his].used : 0;

        if (his >= 0) {
            NowDngMap->boxes[his].used = 0;
        }
        NowDngMap->DrawItemBox(pos);
        if (his >= 0) {
            NowDngMap->boxes[his].used = used;
        }
    }

    // (the tests' line: x-opener-view -BoxBeside)
    if (said != open) {
        said = open;
        std::fprintf(stderr, "coop-hisbox: P1's %s chest presentation on her copy: %d other box(es) in reach, %d drawn, his box %d left out\n",
                     open == 1 ? "small" : "big", others, hides ? 0 : others, his);
    }
}

float PartnerBoxDrawn() {
    return g_fx != nullptr ? g_fx->box_drawn : 0.0f;
}

float PartnerGlowDrawn() {
    return g_fx != nullptr && g_partner.busy > 0 ? g_fx->glow_drawn : 0.0f;
}

bool PartnerPresentationLight(const float *stood, sceVu0FMATRIX light, sceVu0FMATRIX colour) {
    // Her old path's own light (unlinked, present_lockstep), else retail's from her copy's engine when the Atla is hers (coop-opt7e).
    return AtlaLight(stood, light, colour) || PresentLight(ActorNumber(1), stood, light, colour);
}

float PartnerLightDrawn() {
    return g_atla != nullptr && g_atla->ticks > 0 ? g_atla->lit : 0.0f;
}

float PartnerItemDrawn() {
    return g_fx != nullptr && g_partner.busy > 0 ? g_fx->item_drawn : 0.0f;
}

bool PartnerAtlaPresenting() {
    // (coop-opt7: the world's; her effect on the others' copies is draw only)
    return PresentKindOf(2) == kPresentAtla || (!PresentLocalOn() && g_atla != nullptr && g_atla->ticks > 0);
}

namespace {

// As retail's opening ends (BtGetTreasurebox*_Loop's Cross): she stands 10 in front of the box, facing it, and the item's
// message goes (if it is still hers on show).
void EndOpening() {
    PlayMotion(0, 0);

    if (!Mutated("chestturn")) { // as retail's presentation ends: 10 in front of the box, facing it
        g_partner.chara->frame->SetRotation(0.0f, -PI, 0.0f);
        g_partner.heading[0] = 0.0f;
        g_partner.heading[1] = 0.0f;
        g_partner.heading[2] = -1.0f;
    }

    if (g_partner.open_message > 0 && SystemMesNo == g_partner.open_message) {
        ClearSystemMes();
    }

    std::printf("coop: partner closes her chest's presentation after %d ticks\n", g_partner.open_tick);
    g_partner.busy = 0;
    g_partner.open_tick = -1;
    g_partner.open_hold = false;
    g_partner.open_message = -1;
    FreeOpeningItem();
    g_pose.after = 15;
    g_pose.swings = g_partner.swings;
}

// coop-atlaopen: one tick of her opening as retail's (kOpenHold): its sounds, then her pose and the item's message held
// until her Cross or Circle. True while it lasts.
bool StepOpening(int pressed) {
    if (g_partner.open_hold) {
        StepOpeningFx();

        if ((pressed & (kInputCross | kInputCircle)) != 0) {
            EndOpening();
            return ClosingPressHeld();
        }

        return true;
    }

    StepOpeningFx();
    g_partner.open_tick++;

    // coop-actorswap: where her opening's motion stands a few ticks in (r-chest-motion reads it).
    if (g_partner.open_tick == 6) {
        const int motion = g_partner.open_big ? kOpenBig : kOpenSmall;
        float     now = 0.0f;
        int       start = 0, end = 0;

        if (ChestMotionAt(motion, &now, &start, &end)) {
            std::fprintf(stderr, "coop: partner opening motion %d at frame %.1f, 6 ticks in (frames %d to %d)\n", motion, now, start, end);
        }
    }

    if (g_partner.open_tick == g_pose.ticks / 2 && BodyMatrix(g_pose.body)) {
        g_pose.sampled = true;
    }

    if (g_partner.open_tick == (g_partner.open_big ? kOpenSoundBig : kOpenSoundSmall)) {
        SndSePlay(g_partner.open_big ? SE_CHEST_OPEN_BIG : SE_CHEST_OPEN_SMALL, -1, 0);
        PartnerSpecialSound(2, 2);
    }

    if (g_partner.open_tick >= kOpenHold) {
        MessageBy by(2); // coop-itemprompt: on her screen only
        int       motion = (g_partner.open_big ? kOpenBig : kOpenSmall) + 1;

        if (ChestMotionFrames(motion) > 0) {
            PlayMotion(motion, ChestPlayFlags(2));
        }

        // Retail's waits for Cross on player 1's pad (input_key 1); hers is up until her own press clears it.
        ItemGetMes(g_partner.open_item, g_partner.open_volume != 0 ? g_partner.open_volume : -1, -1, 0);
        g_partner.open_message = SystemMesNo;
        g_partner.open_hold = true;
        std::printf("coop: partner holds up item %d after %d ticks\n", g_partner.open_item, g_partner.open_tick);
    }

    return true;
}

} // namespace

// The partner's opening: true while it lasts, holding the partner still; then back to standing.
bool StepBusy(const InputPadState &pad) {
    int pressed = pad.buttons & ~g_partner.open_buttons;

    g_partner.open_buttons = pad.buttons;

    int &pose_after = g_pose.after; // (coop-warmreset, WS7: a close in the job before logs no pose in this one)
    COOP_TEST_TRACE_RESET(pose_after, 0);
    if (g_pose.after > 0 && --g_pose.after == 0) {
        LogPose();
    }

    // coop-opt7 step 3: her own presentation (the world half; on her copy its engine), held until its end command
    if (PresentStepFor(2)) {
        return true;
    }

    if (StepAtlaFx()) { // coop-openercam: she stands through her Atla's presentation
        return true;
    }

    if (StepKeyPresentation(pressed)) { // coop-p2hurt: her gate key's
        return true;
    }

    if (g_partner.busy == 0) {
        if (g_fx != nullptr) {
            g_fx->playing = -1; // coop-openercam
        }
        return false;
    }

    if (g_partner.open_tick >= 0) { // coop-atlaopen
        return StepOpening(pressed);
    }

    StepOpeningFx(); // coop-openercam

    if (g_partner.busy == g_pose.ticks / 2 && BodyMatrix(g_pose.body)) {
        g_pose.sampled = true;
    }

    if (--g_partner.busy == 0) {
        PlayMotion(0, 0);

        if (!Mutated("chestturn")) { // as retail's presentation ends: 10 in front of the box, facing it
            g_partner.chara->frame->SetRotation(0.0f, -PI, 0.0f);
            g_partner.heading[0] = 0.0f;
            g_partner.heading[1] = 0.0f;
            g_partner.heading[2] = -1.0f;
        }

        g_pose.after = 15;
        g_pose.swings = g_partner.swings;
        return false;
    }

    return true;
}

// While the partner opens a chest its frame stands on the box, unturned, as retail's presentation
// stands the player's (StartOpening); Walk calls it after putting the frame where the partner is.
void PoseBusy() {
    if (g_partner.busy == 0 || Mutated("chestturn")) {
        return;
    }

    g_partner.chara->frame->SetPosition(g_partner.pos[0], g_partner.pos[1], g_partner.pos[2] - kOpenFront);
    g_partner.chara->frame->SetRotation(0.0f, 0.0f, 0.0f);
}

// coop-opt7 step 3 (debug\notes\opt7-design.md; Jep 11-09): her chest's and her Atla's presentation is her copy's alone, retail's
// own Init and Loop run there as its engine (coop_opener.cpp, as player 1's); these are the world half's moves of her body, on
// every copy alike (g_partner, the state set's). others: this copy is not hers; it draws her chest's glow and item and her Atla's
// effect where she stands (coop-chestseen, coop-actorswap), draw only, never their camera, chime or messages.
void PartnerPresentChest(int entry, bool big, int item, bool posed, bool others) {
    const TREASURE_BOX &box = NowDngMap->boxes[entry];

    if (!posed) { // refused: the box stays shut, she stands where she is until her own close (kCmdPresent)
        std::printf("coop: partner's chest %d refused her: she stands for her own message\n", entry);
        return;
    }
    StartOpening(box.pos, big, true, others);
    g_partner.busy = 1;
    g_partner.open_tick = 0;
    g_partner.open_big = big;
    g_partner.open_hold = false;
    g_partner.open_message = -1;
    g_partner.open_box = entry;
    g_partner.open_item = item;
    g_partner.open_volume = 0;
    if (others) {
        LoadOpeningItem(box.pos, big, item); // ("coop: partner chest item <its model>": the tests' anchor)
    } else {
        std::printf("coop: partner chest item %d (her presentation on her copy)\n", item); // (the same anchor on her copy)
    }
    std::printf("coop: partner opens %s box %d: item %d (her presentation on her copy)\n", big ? "large" : "small", entry, item);
}

void PartnerPresentAtla(int entry, bool others, float *turn) {
    const ATRA_BOLL &ball = NowDngMap->atra[entry];

    AtlaStanceAt(ball.pos);
    AtlaPoseStart();
    *turn = g_atla_turn;
    if (others) {
        StartAtlaFx(ball.pos, true);
    }
    // (the tests' line, as her old path's: x-atla-open reads the ball, her place, her facing and her step back)
    std::printf("coop: partner Atla presentation starts at (%.1f %.1f %.1f), she stands (%.1f %.1f) facing %.3f, effect turned %.3f, steps to (%.1f %.1f) (her presentation on her copy, member %d)\n",
                ball.pos[0], ball.pos[1], ball.pos[2], g_partner.pos[0], g_partner.pos[2], g_atla_face, g_atla_turn,
                g_atla_slide > 0 ? g_atla_slide_to[0] : g_partner.pos[0], g_atla_slide > 0 ? g_atla_slide_to[1] : g_partner.pos[2],
                g_partner.loaded_chara);
}

bool PartnerPresentStep(int kind, int ticks, bool others) {
    if (kind == kPresentAtla) {
        if (others && g_atla != nullptr && g_atla->ticks > 0) { // (draw only: its frame for the next draw)
            g_atla->chara->Step();
            g_atla->ticks++;
        }
        if (g_atla_slide > 0) { // coop-actorswap: her step back to retail's distance
            const float t = (float) (kAtlaSlideTicks - --g_atla_slide) / (float) kAtlaSlideTicks;

            g_partner.pos[0] = g_atla_slide_from[0] + (g_atla_slide_to[0] - g_atla_slide_from[0]) * t;
            g_partner.pos[2] = g_atla_slide_from[1] + (g_atla_slide_to[1] - g_atla_slide_from[1]) * t;
            g_partner.chara->frame->SetPosition(g_partner.pos[0], g_partner.pos[1], g_partner.pos[2]);
        }
        if (ticks == 6) { // coop-actorswap: where her grab stands a few ticks in (r-chest-motion reads it)
            float now = 0.0f;
            int   start = 0, end = 0;

            if (g_partner.motion == kAtlaGrab && ChestMotionAt(kAtlaGrab, &now, &start, &end)) {
                std::fprintf(stderr, "coop: partner Atla grab motion %d at frame %.1f, 6 ticks in (frames %d to %d)\n", kAtlaGrab, now, start, end);
            }
        }
        // coop-atlaswap: Toan's pose from the effect's end (frame 129.5, its tick), as retail's step 1 sets it (motion_no 0x2F)
        if (ticks == kAtlaEndTick && g_atla_pose) {
            PlayMotion(kAtlaPoseEnd, ChestPlayFlags(0));
        }
        return false;
    }
    if (g_partner.open_tick < 0) {
        return false; // refused: nothing to step
    }
    if (others) {
        StepOpeningFx(); // the glow's next frame on the others' copies (it stops itself at 79)
    }
    if (g_partner.open_hold) {
        return false;
    }
    g_partner.open_tick++;
    if (g_partner.open_tick == 6) { // coop-actorswap: where her opening's motion stands a few ticks in (r-chest-motion reads it)
        const int motion = g_partner.open_big ? kOpenBig : kOpenSmall;
        float     now = 0.0f;
        int       start = 0, end = 0;

        if (ChestMotionAt(motion, &now, &start, &end)) {
            std::fprintf(stderr, "coop: partner opening motion %d at frame %.1f, 6 ticks in (frames %d to %d)\n", motion, now, start, end);
        }
    }
    if (g_partner.open_tick == g_pose.ticks / 2 && BodyMatrix(g_pose.body)) {
        g_pose.sampled = true;
    }
    if (g_partner.open_tick < kOpenHold) {
        return false;
    }

    const int motion = (g_partner.open_big ? kOpenBig : kOpenSmall) + 1;

    if (ChestMotionFrames(motion) > 0) {
        PlayMotion(motion, ChestPlayFlags(2));
    }
    g_partner.open_hold = true;
    std::printf("coop: partner holds up item %d after %d ticks\n", g_partner.open_item, g_partner.open_tick);
    return true;
}

void PartnerPresentEnd(int kind, bool posed) {
    if (kind == kPresentAtla) {
        if (g_atla != nullptr) {
            g_atla->ticks = 0; // (the others' effect)
        }
        if (g_partner.motion == kAtlaGrab || g_partner.motion == kAtlaHold || g_atla_pose) {
            PlayMotion(0, 0);
        }
        g_atla_pose = false;
        g_atla_slide = 0;
        if (!Mutated("atlaside")) { // as retail's end turns the opener round, toward the ball (g_atla_end)
            g_partner.chara->frame->SetRotation(0.0f, g_atla_end, 0.0f);
            g_partner.heading[0] = sinf(g_atla_end);
            g_partner.heading[1] = 0.0f;
            g_partner.heading[2] = cosf(g_atla_end);
        }
        return;
    }
    if (posed && g_partner.open_tick >= 0) {
        EndOpening();
    }
    if (g_fx != nullptr) {
        g_fx->playing = -1;
    }
}

// coop-openpos (Jep's 2026-10-28 playtest: her chest opening "starts too far away from the chest"): where her character goes
// for the tick's step and draw (CoopStep's partner->SetPosition, which sets the frame and shadow too): her place, but on the box
// while she opens a chest, as PoseBusy puts the frame. That step put the frame back on her place, 10 in front, every tick, so
// the opening's motion carried her body out to 19 from the box instead of retail's 10 (LogPose: "body 19.1 from it").
// DC_COOP_MUTATE=openframe: her place, as before.
void PartnerFramePlace(float *at) {
    sceVu0CopyVector(at, g_partner.pos);

    if (g_atla_pose) { // coop-atlaswap: Toan's Atla pose stands on the ball
        at[0] = g_atla_pose_ball[0];
        at[2] = g_atla_pose_ball[1];
        return;
    }

    if (g_partner.busy > 0 && !Mutated("chestturn") && !Mutated("openframe")) {
        at[2] -= kOpenFront;
    }
}

// Gold and items on the floor come to the partner as they come to the player
// (CRandomItem::CheckPosition, which only measures from the player): within 5 units, gold is taken
// into the shared purse, an item raises its pickup event, which the player's next tick shows as it
// shows its own. An item the party has no room for stays where it is, without a message (the player's
// walking onto it gives that). Reads only the floor and party, so linked copies agree.
void PickUp() {
    if (Mutated("pickup")) {
        return;
    }

    CRandomItem *items = RandomItem;
    int          gold = 0;

    if (items == nullptr) {
        return;
    }

    // Test hook (DC_COOP_DROP): once, 50 gold and a peridot fall at the partner's feet, out of the
    // player's reach, so only the partner can take them.
    static const bool drop = TestEnv("DC_COOP_DROP") != nullptr;
    static bool       dropped = false;

    if (drop && !dropped) {
        sceVu0FVECTOR at;

        dropped = true;
        sceVu0CopyVector(at, g_partner.pos);
        items->Set(at, 100, 50, -1);
        at[2] += 2.0f;
        items->Set(at, 101, -1, ITEM_ATTACH_PERIDOT);
        std::printf("coop-debug: dropped 50 gold and item %d at the partner's feet, player %.0f away\n", ITEM_ATTACH_PERIDOT,
                    DistVector(g_partner.pos, CharaMain.pos));
    }

    for (int i = 0; i < 32; i++) {
        if (items->id[i] == -1 || items->state[i] != RandomItemState::WAITING || items->pickup_blocked[i] != 0 ||
            DistVector(items->position[i], g_partner.pos) > 5.0f) {
            continue;
        }

        if (items->amount[i] == -1 && items->item_no[i] != -1) {
            // coop-inventory: a bag item goes straight into P2's own bag, with its message; a full bag
            // leaves it lying. Story items and attachments still go to the party as below.
            if (PartnerKeeps(items->item_no[i])) {
                if (PartnerGetItem(items->item_no[i], -1, true)) {
                    items->id[i] = -1;
                    std::printf("coop: partner picks up item %d into its own bag\n", items->item_no[i]);
                }

                continue;
            }

            if (((CDngStatusData *) UserStatus)->CheckItemGet(items->item_no[i]) == 0) {
                items->id[i] = -1;
                std::printf("coop: partner picks up item %d\n", items->item_no[i]);

                // coop-p2keys: a gate key she picks up goes into the party's pack with its message and sound,
                // not by the player's tick, which plays Toan's "got the key" presentation and holds both players
                // until player 1's Cross (BtGetGateKey_Loop): her pickup must not stop him.
                // DC_COOP_MUTATE=keyscene (x-dungeon-p2 keydoor's twin) raises the pickup event as before.
                if (GateKey(items->item_no[i]) && !Mutated("keyscene")) {
                    MessageBy by(2);
                    ((CDngStatusData *) UserStatus)->GetItem(items->item_no[i], 0);

                    // coop-p2hurt: with her own presentation, its message comes as she holds the key up.
                    if (!StartKeyPresentation(items->item_no[i])) {
                        ItemGetMes(items->item_no[i], -1, 0x96, 0);
                        DngMessMan.hide_timer = 0x96;
                    }

                    SndSePlay(SE_GATE_KEY_GET, -1, 0);
                    std::printf("coop: partner's gate key %d into the party's pack, no presentation\n", items->item_no[i]);
                } else {
                    items->pickup_event[i] = items->item_no[i];
                    PartnerPickupRaised(items->item_no[i]); // coop-itemprompt: its message, shown by his tick, is hers
                }
            }

            continue;
        }

        items->state[i] = RandomItemState::TAKEN;

        if (items->item_no[i] == -1) {
            SndSePlay(SE_ITEM_GET, -1, 0);
        }

        gold += items->amount[i];
        items->position[i][1] += 5.0f;
        HitValueEntry(NowHitValue, items->position[i], items->amount[i], HIT_VALUE_GOLD, NULL);
        items->position[i][1] -= 5.0f;
        std::printf("coop: partner picks up %d gold\n", items->amount[i]);
    }

    if (gold > 0) {
        PartnerGold(gold); // coop-inventory: P2's own purse, or the party's
    }
}

// Cross where the player's Cross would act does what the player's does (DunMoveChara's GAME_TASK_PLAY
// branch), from where the partner stands: an event slot's script (doors, gates, a member's door), else
// the map event in reach: a chest opens (small, large or trapped, into the shared inventory), a mimic
// wakes, an Atla is taken (by any member, TakeAtla). The task starts on the next tick, as the player's
// does; the sequences that follow are retail's and show the player. False when nothing was in reach.
bool Interact() {
    // coop-opt7 step 3: nothing new while her own presentation runs (the walk holds her; a test's direct call too)
    if (PresentKindOf(2) != kPresentNone) {
        return false;
    }
    if (CoopDebug()) {
        float nearest = 1.0e9f;

        for (int i = 0; i < 48; i++) {
            if (NowDngMap->events[i].kind != DNG_EVENT_NONE) {
                nearest = std::min(nearest, DistVector(NowDngMap->events[i].pos, g_partner.pos));
            }
        }

        std::printf("coop-debug: partner Cross at (%.0f %.0f %.0f): task %d can_act %d locked %d, nearest event %.1f\n",
                    g_partner.pos[0], g_partner.pos[1], g_partner.pos[2], gameTask, BtActStatus.can_act,
                    BtActStatus.movement_locked, nearest);
    }

    // coop-chestbeside (Jep's 2026-10-28 playtest: "Xiao can't open a chest while toan is grabbing an atla"): his chest, Atla,
    // gate key and attachment presentations are his alone (she walks through them, PartnerPlaysThrough), so beside them her
    // Cross does what needs no game task: a plain chest (OpenBox) or an Atla (TakeAtla), each her own presentation. A script,
    // a prompt or anything that would start a task still waits for play. His can_act and movement lock are his presentation's,
    // not hers. DC_COOP_MUTATE=chestbeside (twin): nothing until play, as before.
    const bool beside = gameTask != GAME_TASK_PLAY && PartnerPlaysThrough(gameTask) && !Mutated("chestbeside");

    if (!beside && (gameTask != GAME_TASK_PLAY || BtActStatus.can_act == 0 || BtActStatus.movement_locked != 0)) {
        return false;
    }

    CDungeonEventData *state = beside ? nullptr : NowEventMan->SearchDataSlotPos2(g_partner.pos);

    // coop-party: an ally spot (event chr<N>_..., e.g. chr1_3 is Xiao's) for the partner's own member.
    // Its script reads the player's member (_GET_NOW_USER_ID) and quietly does nothing for anyone else,
    // so the partner's Cross asks the player to swap instead: yes, and the player is that member and can
    // use the spot. DC_COOP_MUTATE=allyspot runs the script as before, for the test's twin.
    if (state != nullptr && state->event->name[0] == 'c' && state->event->name[1] == 'h' && state->event->name[2] == 'r' &&
        state->event->name[3] >= '0' && state->event->name[3] <= '5' && state->event->name[4] == '_' && !Mutated("allyspot")) {
        int member = state->event->name[3] - '0';

        if (member == g_partner.loaded_chara && member != UserStatus->cur_chara && member < UserStatus->party_size) {
            AskSwap(1, UserStatus->cur_chara, member);
            std::printf("coop: partner found %d's spot %s; asking the player to swap\n", member, state->event->name);
            return true;
        }
    }

    // coop-party: a member's event (chara_no set: the help only that member gives, its script run with
    // the player as the actor) for the partner's own member: as at an ally spot, the player is asked to
    // swap instead, and uses it as that member. DC_COOP_MUTATE=memberdoor runs it as before (twin).
    if (state != nullptr && state->event->chara_no >= 0 && state->event->chara_no < 6 && !Mutated("memberdoor")) {
        int member = state->event->chara_no;

        if (member == g_partner.loaded_chara && member != UserStatus->cur_chara && member < UserStatus->party_size) {
            AskSwap(1, UserStatus->cur_chara, member);
            std::printf("coop: partner at %d's event %s; asking the player to swap\n", member, state->event->name);
            return true;
        }
    }

    if (state != nullptr && state->event->script_no != -1 && state->event->fade == 0 &&
        !(state->event->chara_no != -1 && state->chara_done == g_partner.loaded_chara)) {
        // coop-p2prompt: the exit gate asks her on her own screen first, and a slot whose Cross only shows a message (the
        // gate's lock: "It's locked") shows it on her screen; neither starts the game-wide script (coop_useprompt.cpp).
        if (std::strncmp(state->event->name, "out", 3) == 0 && OpenGatePrompt(2, state)) {
            return true;
        }

        if (state->event->chara_no == -1 && CrossMessagePrompt(2, state->event->script_no)) {
            return true;
        }

        BtEventInfo.script_no = state->event->script_no;
        BtEventInfo.ext_memory = state->event->ext_mem;
        BtEventInfo.event_marker = true;
        sceVu0CopyVector(BtEventInfo.position, state->pos);
        sceVu0CopyVector(BtEventInfo.direction, state->dir);

        if (state->event->chara_no != -1) {
            if (g_partner.loaded_chara == state->event->chara_no) {
                state->chara_done = g_partner.loaded_chara;
            }

            BtEventInfo.chara_help = true;
            BtEventInfo.action_mode = 0;
        } else {
            BtEventInfo.action_mode = 1;
        }

        gameTask = GAME_TASK_SCRIPT_START;
        g_her_script_start = true; // coop-skeldoor: hers, not his (HisCrossBeside)
        std::printf("coop: partner starts event script %d\n", (int) BtEventInfo.script_no);
        return true;
    }

    int active = NowDngMap->GetActiveIvent((CFrameVu1 *) g_partner.chara->frame);

    if (active == -1) {
        return false;
    }

    int entry_no = NowDngMap->events[active].index;

    switch (NowDngMap->events[active].kind) {
        case DNG_EVENT_TREASURE_BOX:
            // coop-party: the partner opens a chest itself (OpenBox); only a trapped one still runs its
            // script, as the player's does.
            if (!Mutated("chest") && (NowDngMap->boxes[entry_no].kind != TREASURE_BOX_LARGE || NowDngMap->boxes[entry_no].trap_no == 0)) {
                // coop-opt7 step 3: linked, her presentation is her copy's engine, the world half every copy's (else her old path)
                if (!PresentStartFor(2, active)) {
                    OpenBox(active, entry_no, NowDngMap->boxes[entry_no].kind == TREASURE_BOX_LARGE);
                }
                return true;
            }

            if (beside) {
                return false; // coop-chestbeside: a trapped or locked one waits for play
            }

            // coop-p2prompt: a locked or trapped large chest (and the clown's) asks her in her own prompt, beside the game.
            if (!Mutated("chest") && OpenChestPrompt(2, active, entry_no)) {
                return true;
            }

            iventActive = active;
            BtActStatus.motion_no = 0;

            if (NowDngMap->boxes[entry_no].kind == TREASURE_BOX_LARGE) {
                if (NowDngMap->boxes[entry_no].trap_no == 0) {
                    gameTask = GAME_TASK_BOX_BIG_INIT;
                    SndSePlay(MENU_SOUND_CONFIRM, -1, 0);
                } else {
                    sceVu0FVECTOR box_dir = {0.0f, 0.0f, 0.0f, 1.0f};

                    sceVu0CopyVector(BtEventInfo.position, NowDngMap->events[active].pos);
                    sceVu0CopyVector(BtEventInfo.direction, box_dir);
                    BtEventInfo.ext_memory = false;
                    BtEventInfo.treasure_box = entry_no;
                    BtEventInfo.action_mode = 1;

                    if (NowDngMap->boxes[entry_no].trap_no == 5) {
                        NowDngMap->events[active].kind = DNG_EVENT_NONE;
                        BtEventInfo.script_no = 0x10;
                    } else {
                        BtEventInfo.script_no = 0xF;
                    }

                    coop::TrapOpenedByPartner(); // coop: combat (the trap goes off at P2)
                    gameTask = GAME_TASK_SCRIPT_START;
                    ResetMovePower();
                }
            } else {
                gameTask = GAME_TASK_BOX_SMALL_INIT;
                SndSePlay(MENU_SOUND_CONFIRM, -1, 0);
            }

            std::printf("coop: partner opens box %d\n", entry_no);
            return true;
        case DNG_EVENT_ATRA:
            if (NowDngMap->atra[entry_no].used == 0) {
                return false;
            }

            // coop-p2atla: the partner takes an Atla itself, as any member (Jep 2026-10-25: "P2 can't open the
            // atla"; world state is shared). Retail's presentation poses the player and moves the camera to
            // it: with the partner as Toan after a swap it loaded Toan's Atla motions onto the player's other
            // member and crashed. DC_COOP_MUTATE=p2atla (Toan only, as before) and p2atlaswap (the partner as
            // Toan starts retail's presentation) are the twins.
            if (!(Mutated("p2atla") && g_partner.loaded_chara != CHARA_TOAN) &&
                !(Mutated("p2atlaswap") && g_partner.loaded_chara == CHARA_TOAN)) {
                if (!PresentStartFor(2, active)) { // coop-opt7 step 3
                    TakeAtla(active, entry_no);
                }
                return true;
            }

            if (beside) {
                return false; // coop-chestbeside
            }

            iventActive = active;
            BtActStatus.motion_no = 0;

            if (g_partner.loaded_chara == CHARA_TOAN) {
                gameTask = GAME_TASK_ATRA_GET_INIT;
                SndSePlay(MENU_SOUND_CONFIRM, -1, 0);
            } else {
                MessageBy by(2);
                NotGetAtraMes(g_partner.loaded_chara, 0x5A);
                DngMessMan.hide_timer = 0x5A;
            }

            return true;
        case DNG_EVENT_MIMIC: {
            int item_no = NowDngMap->boxes[entry_no].item_no;

            if (item_no >= 0 && item_no < 0x11) {
                NowMonstorUnit->monster[item_no].revealed = 1;
            }

            NowDngMap->boxes[entry_no].used = 0;
            NowDngMap->events[active].kind = DNG_EVENT_NONE;
            return true;
        }
        default:
            return false;
    }
}

// coop-p2keys: Square where the player's Square would use an item (DunMoveChara: an event slot whose hold is
// set, such as the gate's lock that wants the floor's key) does what the player's does, from where the partner
// stands: the slot's script starts in its item mode (action_mode 2). Its window offers the party's pack, where
// the keys and story items are for both players (PartnerKeeps never takes them), and her pad answers it
// (MenuNotePrompt). Jep: "P2 should be able to use keys too". False when no such slot is in reach.
// DC_COOP_MUTATE=keydoor (x-dungeon-p2 keydoor's twin): her Square ignores the slot, as before.
bool InteractItem() {
    if (gameTask != GAME_TASK_PLAY || BtActStatus.can_act == 0 || BtActStatus.movement_locked != 0 || Mutated("keydoor")) {
        return false;
    }

    CDungeonEventData *state = NowEventMan->SearchDataSlotPos2(g_partner.pos);

    if (state == nullptr || state->event->script_no == -1 || state->hold == 0 || state->event->fade != 0 ||
        (state->event->chara_no != -1 && state->chara_done == g_partner.loaded_chara)) {
        return false;
    }

    BtEventInfo.script_no = state->event->script_no;
    BtEventInfo.ext_memory = state->event->ext_mem;
    BtEventInfo.event_marker = true;
    BtEventInfo.action_mode = 2;
    sceVu0CopyVector(BtEventInfo.position, state->pos);
    sceVu0CopyVector(BtEventInfo.direction, state->dir);
    gameTask = GAME_TASK_SCRIPT_START;
    std::printf("coop: partner uses an item at event %s (script %d)\n", state->event->name, (int) BtEventInfo.script_no);
    return true;
}

// coop-keypick (Jep, 2026-10-25: Toan's game must not pause and say "Xiao is choosing..." while she picks a key, and
// "the prompt should show both the keypicker and the regular items"): her Square at an event slot that wants an item
// (a gate's lock, or any slot whose hold is set) opens a picker of her own instead of the slot's Use Item window,
// which ran inside the slot's script as a game-wide task and stopped both linked copies while she chose. The picker
// lists, as one list, the party's shared keys and story items (from the party's pack, SharedItem, marked "shared")
// and then her own bag's items (PartnerPack: her bag with separate bags, else the party's pack). It steps on pad 2
// alone on both copies (so they agree, as the wheel does) and is drawn on her copy only; the task stays play and
// player 1 walks on.
// At a gate's lock her Cross on the floor's gate key does what the lock's script does (read from the retail runs:
// the window takes the key away, the lock's slot switches off, the gate's way out on), on both copies on one tick,
// with no script; any other item: "Nothing happens.", nothing taken. At any other slot her pick starts the slot's
// script in its item mode with her answer ready (KeyPickAnswer, where the script asks for the window): the window
// never opens, and an item the script takes comes out of the pack she picked it from (her bag for her own items).
// The script's lines after that still run as a game-wide task with her prompt (MenuNotePrompt), as her Cross on a
// door does. First to act wins: a lock no longer on (player 1's Square used the key first) or an item gone refuses
// her pick with a line.
// DC_COOP_MUTATE=keypause: her Square runs the slot's script and its window, as on coop-p2keys (twin).
// DC_COOP_MUTATE=keyshared: the picker lists only the party's pack, as the window did (twin: her bag's items missing).
// DC_COOP_KEYDOOR=script (test hook): a gate's lock is treated as any other slot (her pick answers its script).
namespace {

struct PickEntry {
    std::int16_t item;
    bool         shared; // from the party's pack (a key or story item), else from her own items
};

struct KeyPick {
    bool           open = false;
    int            slot = -1;       // the slot's event slot
    CDungeonEvent *event = nullptr; // its event, so a new floor closes the picker
    bool           gate = false;    // a gate's lock: the key opens it here, no script
    PickEntry      items[24] = {};
    int            count = 0;
    int            cursor = 0;
    std::uint16_t  held = 0;
    char           note[64] = {};   // the last line, on her copy
    int            note_ticks = 0;
    int            answer = -1;     // her pick, waiting for the slot's script to ask (KeyPickAnswer)
    bool           answer_shared = false;
    bool           town = false;    // coop-doorprompt: at Norune's cave door, no event slot
    bool           town_closed = false; // coop-doorprompt: and closed this tick (its Cross is the picker's, not a door's)
    bool           apart = false;       // coop-doorfix: opened apart (a split or a merge since closes it)
};

int g_cave_key_user = 0; // coop-doorprompt: the player whose pick of the cave key the town has not taken yet

// One picker for each player (coop-keypick, Jep: "just like Xiao's game shouldn't pause when p1 is interacting with an
// item"): player 1's opens from DunMoveChara's Square at a slot that wants an item, steps on pad 1 in MenuTick, and
// while open MenuFilterPort keeps his pad from the game (Toan stands). g_who names the one a call is about.
KeyPick g_picks[2];
int     g_who = 2;

KeyPick &Cur() {
    return g_picks[g_who - 1];
}

void PickNote(const char *text) {
    std::snprintf(Cur().note, sizeof Cur().note, "%s", text);
    Cur().note_ticks = 150;
    std::printf("coop: %s key picker: %s\n", g_who == 1 ? "player 1's" : "partner's", text);
}

std::string PickItemName(int item) {
    return item > 0 ? dc::battlemenu::detail::DecodeItemNamePort(item, "?") : std::string("-");
}

void ClosePick() {
    Cur().open = false;
    Cur().town_closed = Cur().town;
    Cur().town = false;
    Cur().slot = -1;
    Cur().event = nullptr;
}

void AddPick(int item, bool shared) {
    for (int k = 0; k < Cur().count; k++) {
        if (Cur().items[k].item == item && Cur().items[k].shared == shared) {
            return;
        }
    }

    if (Cur().count < 24) {
        Cur().items[Cur().count++] = PickEntry{(std::int16_t) item, shared};
    }
}

// Player 1's items are the party's pack; hers are her own bag, the shared ones the party's. coop-keyring: the shared ones
// come from the party's ring wherever this copy keeps it (her copy apart plays her as the game's player 1, with her bag
// in the game's pack and the ring beside it). DC_COOP_MUTATE=ringpick (twin): the game's pack, as before.
ITEM_PACK &PickPack(bool shared) {
    if (shared && !Mutated("ringpick")) {
        return SharedRing();
    }

    return shared || g_who == 1 ? UserStatus->item_pack : PartnerPack();
}

// One of item out of pack, as the window's vanish takes it (the slot set to 0). False when none is left. coop-keyfloor:
// a floor key only from a slot tagged for this floor (KeyFitsHere).
bool TakeOne(ITEM_PACK &pack, int item) {
    for (int i = 0; i < pack.num && i < 103; i++) {
        if (pack.item[i] == item && KeyFitsHere(pack, i)) {
            pack.item[i] = 0;
            return true;
        }
    }

    return false;
}

bool Holds(const ITEM_PACK &pack, int item) {
    for (int i = 0; i < pack.num && i < 103; i++) {
        if (pack.item[i] == item && KeyFitsHere(pack, i)) {
            return true;
        }
    }

    return false;
}

// coop-keyfloor: pack holds item only as keys found on other floors: true, with the line (and the first one's floor).
bool OtherFloorsOnly(const ITEM_PACK &pack, int item) {
    int map = -1;
    int floor = -1;

    for (int i = 0; i < pack.num && i < 103; i++) {
        if (pack.item[i] != item) {
            continue;
        }

        if (KeyFitsHere(pack, i)) {
            return false;
        }

        if (map < 0) {
            KeyFloorOf(pack, i, &map, &floor);
        }
    }

    if (map < 0) {
        return false;
    }

    std::printf("coop: %s key %d is from map %d floor %d, not this floor (map %d floor %d): refused\n", g_who == 1 ? "player 1's" : "partner's",
                item, map, floor, selectMapNo, UserStatus != nullptr ? (int) UserStatus->cur_floor : -1);
    PickNote("That key is from another floor.");
    return true;
}

// The lock's result, as its script gives it: the key out of the pack, the lock off, the way out with the lock's
// number on.
bool UseGateKey(const PickEntry &entry) {
    CDungeonEventData &lock = NowEventMan->event[Cur().slot];

    if (gameTask != GAME_TASK_PLAY || lock.event != Cur().event || !lock.enabled || lock.switch_on == 0) {
        PickNote("The lock is already open.");
        return false;
    }

    if (!TakeOne(PickPack(entry.shared), entry.item)) {
        PickNote("The key is gone.");
        return false;
    }

    const char *suffix = std::strchr(lock.event->name, '_');
    int         way_out = -1;

    for (int i = 0; suffix != nullptr && i < 96; i++) {
        CDungeonEventData &data = NowEventMan->event[i];
        const char        *own = data.event != nullptr ? std::strchr(data.event->name, '_') : nullptr;

        if (data.event != nullptr && data.enabled && std::strncmp(data.event->name, "out", 3) == 0 && own != nullptr &&
            std::strcmp(own, suffix) == 0) {
            way_out = i;
            break;
        }
    }

    lock.Stop();

    if (way_out >= 0) {
        NowEventMan->event[way_out].Start();
    }

    SndSePlay(MENU_SOUND_CONFIRM, -1, 0);
    std::printf("coop: %s uses key %d at event %s, way out %s, the game plays on (task %d)\n", g_who == 1 ? "player 1" : "partner",
                entry.item, lock.event->name,
                way_out >= 0 ? NowEventMan->event[way_out].event->name : "none", (int) gameTask);
    PickNote("The gate opens.");
    return true;
}

// Any other slot: its script in item mode, as InteractItem starts it, with her pick as the window's answer.
bool StartWithAnswer(const PickEntry &entry) {
    CDungeonEventData &state = NowEventMan->event[Cur().slot];

    if (gameTask != GAME_TASK_PLAY || state.event != Cur().event || !state.enabled || state.hold == 0) {
        PickNote("Nothing to use it on now.");
        return false;
    }

    if (!Holds(PickPack(entry.shared), entry.item)) {
        PickNote("The item is gone.");
        return false;
    }

    Cur().answer = entry.item;
    Cur().answer_shared = entry.shared;
    BtEventInfo.script_no = state.event->script_no;
    BtEventInfo.ext_memory = state.event->ext_mem;
    BtEventInfo.event_marker = true;
    BtEventInfo.action_mode = 2;
    sceVu0CopyVector(BtEventInfo.position, state.pos);
    sceVu0CopyVector(BtEventInfo.direction, state.dir);
    gameTask = GAME_TASK_SCRIPT_START;
    MenuNotePrompt(g_who == 2);
    std::printf("coop: %s key picker answers event %s (script %d) with item %d from %s\n", g_who == 1 ? "player 1's" : "partner's",
                state.event->name, (int) BtEventInfo.script_no, entry.item, entry.shared ? "the party's pack" : "her own items");
    return true;
}

// coop-keydoor (Jep's 2026-10-27 run, the Bone Key door on Divine Beast Cave B1: her key on it blacked Toan's screen with
// "Xiao is choosing..." and gave her his pad while the door opened; his own picker, open at the same door, stayed open after
// it): a key door is not a gate's lock but an event slot (ndoorkey) whose script (program 250 or 251, alike in all seven
// dungeons' event.stb) asks the Use Item window for its key and, given it, slides or swings the door's parts open over some
// 70 ticks (sound 233), swaps the closed mesh for the open one and switches its slot off. StartWithAnswer ran it as the
// game's one script task: both copies stopped, her prompt over his screen.
// Now a slot whose script does, after its item window and before its first call, only such world-object steps (DoorScript:
// _GET_OBJHDL, _SET_OBJHDL_POS/ROT/DRAW_FLAG, _PLAY_SE, _SET_EVENT_SW) runs beside the game: an interpreter of the user's own
// (g_doors) steps the slot's program a wait at a time from each play tick (KeyDoorTick), on both copies on the same tick,
// with object handles of its own and the hold button's action mode and the item window's answer (the pick) put in around
// each step. The pick is taken from the pack it came from when the window lists it, as KeyPickAnswer does. The task stays
// play: neither player is stopped and no prompt shows. The interpreter, its stacks and handles are in the snapshot's state
// set (qa\netplay\snapshot-allow.txt), so a join while a door opens carries it. Any other ext reached ends it (logged); a
// pick the window does not list: "Nothing happens.", nothing taken. Scripts that do more (story items, scenes) run as before.
// DC_COOP_MUTATE=keydoorwide (twin): every slot's script runs as the game's task with her prompt, as before.
constexpr int kDoorStack = 128;
constexpr int kDoorCalls = 32;
constexpr int kDoorExts = 1500; // the event interpreter's table (editloop3.cpp's ext_func__2)

constexpr int kExtActionMode = 1020; // _GET_ACTION_MODE
constexpr int kExtItemWindow = 1021; // _ITEM_USE_WINDOW
constexpr int kExtEventSwitch = 1055; // _SET_EVENT_SW

// coop-skeldoor (Jep's 2026-10-28 online run: "when either player tries confirming the use of an item on a skeleton door that
// isn't correct, nothing happens, no animation or sounds play for either player"): retail's door script answers a wrong item
// by calling the floor script's refusal (given motion 31): the user turns to the camera over 100 ticks with motion 31 (the
// head shake), sound 21 at its 60th, then motion 0 and back over 60 ticks. The door's interpreter now runs that too, beside
// the game: these exts, with the user's body as the script's main character and the user's camera (the game's for player 1,
// hers for player 2; both stepped on both copies) as its camera, the user held still while it plays (DoorRefusing) and its
// sound the user's (SoundOwner). DC_COOP_MUTATE=refusenone (twin): a wrong item stops the door, nothing played, as before.
bool RefusalExt(int id) {
    switch (id) {
        case 66:  // _TURN_CHARA
        case 131: // _GET_NPC_POS
        case 133: // _SET_NPC_MOTION
        case 139: // _GET_NPC_ROT
        case 423: // _GET_CAMERA_POS
        case 425: // _GET_CAMERA_REF
            return true;
        default:
            return false;
    }
}

bool DoorExt(int id) {
    switch (id) {
        case kExtActionMode:
        case kExtItemWindow:
        case 1030: // _GET_OBJHDL
        case 1031: // _SET_OBJHDL_POS
        case 1032: // _SET_OBJHDL_ROT
        case 1033: // _SET_OBJHDL_DRAW_FLAG
        case kExtEventSwitch:
        case 807: // _PLAY_SE
            return true;
        default:
            return false;
    }
}

struct DoorRun {
    bool           on = false;
    bool           started = false; // its first step ran (run); later ones resume
    bool           held = false;    // its slot switched off while it runs (EndDoor gives it back unless it ended)
    bool           refusing = false; // coop-skeldoor: its pick was not the key: the script plays its refusal on the user
    bool           shown = false;    // coop-gateflow: a gate opened before, its doors put open at once (no key taken, no sound)
    int            slot = -1;
    CDungeonEvent *event = nullptr;
    int            item = -1;
    bool           shared = false;
    int            ticks = 0;
    CRunScript     vm;
    RS_STACKDATA   stack[kDoorStack] = {};
    RS_CALLDATA    call[kDoorCalls] = {};
    BT_OBJ_HANDLE  handles[32] = {};
};

DoorRun g_doors[2];
int (*g_door_exts[kDoorExts])(RS_STACKDATA *, int) = {};
bool g_door_refused = false;

int DoorExtRefused(RS_STACKDATA *, int) {
    g_door_refused = true;
    return 1;
}

const RS_PROG_HEADER *DoorProg() {
    const auto *prog = reinterpret_cast<const RS_PROG_HEADER *>(BtSystemScriptFileBuffer.base);

    return prog != nullptr && std::memcmp(prog, "STB", 3) == 0 ? prog : nullptr;
}

// Whether script_no is a key door: past its item window, up to its first call or return, only door steps, one of them
// switching a slot. Read from the bytecode, keeping the operand stack's depth to find each ext's number (its first operand).
// coop-gateflow: follow_calls (a gate's lock, program 161 in every dungeon): a call past the window is read too, its body only
// door steps or a refusal's (the lock calls its door's opening, then its refusal for a wrong item), and the scan goes on after it.
bool DoorCodeFrom(const char *code, const vmcode_t *pc, bool body, bool follow_calls, int depth, bool *window, bool *switched) {
    std::vector<int> pushed;
    auto             pop = [&](int n) { pushed.resize(std::max(0, (int) pushed.size() - n)); };
    auto             indexed = [](int mode) { return mode == RS_ADDR_LOCAL_INDEX || mode == RS_ADDR_POINTER_INDEX ||
                                                     mode == RS_ADDR_LOCAL_INDEX_FLOAT || mode == RS_ADDR_POINTER_INDEX_FLOAT; };

    for (int n = 0; pc != nullptr && n < 1000; n++, pc++) {
        switch (pc->op) {
            case RS_OP_PUSH_CONST:
                pushed.push_back(pc->arg1 == 1 ? pc->arg2 : -1);
                break;
            case RS_OP_LOAD:
            case RS_OP_LOAD_ADDR:
                pop(indexed(pc->arg2) ? 1 : 0);
                pushed.push_back(-1);
                break;
            case RS_OP_POP:
            case RS_OP_JMP_FALSE:
            case RS_OP_JMP_TRUE:
                pop(1);
                break;
            case RS_OP_STORE:
            case RS_OP_ADD:
            case RS_OP_SUB:
            case RS_OP_MUL:
            case RS_OP_DIV:
            case RS_OP_MOD:
            case RS_OP_CMP:
            case RS_OP_AND:
            case RS_OP_OR:
                pop(2);
                pushed.push_back(-1);
                break;
            case RS_OP_PRINT:
                pop(pc->arg1);
                break;
            case RS_OP_EXT: {
                const int at = (int) pushed.size() - pc->arg1;
                const int id = at >= 0 && pc->arg1 > 0 ? pushed[at] : -1;

                pop(pc->arg1);
                if (body) {
                    if (!DoorExt(id) && !RefusalExt(id)) {
                        return false;
                    }
                    *switched = *switched || id == kExtEventSwitch;
                } else if (!*window) {
                    *window = id == kExtItemWindow;
                } else if (!DoorExt(id)) {
                    return false;
                } else {
                    *switched = *switched || id == kExtEventSwitch;
                }
                break;
            }
            case RS_OP_CALL:
                if (*window && follow_calls && depth < 4) {
                    int addr = 0;

                    std::memcpy(&addr, code + pc->arg2, sizeof addr);
                    if (!DoorCodeFrom(code, reinterpret_cast<const vmcode_t *>(code + addr), true, true, depth + 1, window, switched)) {
                        return false;
                    }
                } else if (*window) {
                    return !body && *switched;
                }
                pushed.clear(); // its result, which the next instruction drops
                pushed.push_back(-1);
                break;
            case RS_OP_RET:
            case RS_OP_END:
                if (body) {
                    return true;
                }
                if (*window) {
                    return *switched;
                }
                break;
            default: // NEG, ITOF, FTOI, NOT, SIN, COS keep the depth; WAIT, JMP and the labels touch nothing
                break;
        }
    }

    return false;
}

// The first instruction of program script_no's function (a program that is a stub calling its function: that function's), and
// the code it addresses from; null when the floor's script has no such program.
const vmcode_t *ScriptStart(int script_no, const char **code_out) {
    const RS_PROG_HEADER *prog = DoorProg();

    if (prog == nullptr || script_no < 0) {
        return nullptr;
    }

    const char        *base = reinterpret_cast<const char *>(prog);
    const char        *code = base + prog->code;
    const RS_PROGDATA *entry = reinterpret_cast<const RS_PROGDATA *>(base + prog->prog);
    const vmcode_t    *pc = nullptr;
    int                addr = 0;

    for (int i = 0; i < prog->prog_num && i < 256; i++) {
        if (entry[i].no == script_no) {
            std::memcpy(&addr, base + entry[i].func, sizeof addr);
            pc = reinterpret_cast<const vmcode_t *>(code + addr);
        }
    }

    // A program is a stub that calls its function.
    if (pc != nullptr && pc->op == RS_OP_CALL) {
        std::memcpy(&addr, code + pc->arg2, sizeof addr);
        pc = reinterpret_cast<const vmcode_t *>(code + addr);
    }

    *code_out = code;
    return pc;
}

bool DoorScriptCode(int script_no, bool follow_calls = false) {
    const char     *code = nullptr;
    const vmcode_t *pc = ScriptStart(script_no, &code);
    bool            window = false;
    bool            switched = false;

    return pc != nullptr && DoorCodeFrom(code, pc, false, follow_calls, 0, &window, &switched);
}

// coop-gatekeyno (Jep's 2026-11-04 pack 8 playtest: the Dran's Crest at Divine Beast Cave floor 4's lock gave "Nothing happens."
// on both screens, for either player): the item a script's item window asks for first (_ITEM_USE_WINDOW's operands: the
// answer's address, then the items it wants, constants), read from its function's code; -1 when it asks for none. A gate's
// lock (program 161 in all seven dungeons) names its key there itself, as retail's window takes it: the picker and the doors
// put open go by that, not by the floor's gate_key_no, which in that session no longer matched the lock (each build of the
// floor, both copies; the picker's open line logs both now).
// coop-gatekey52 (x-dungeon-p2-gateopen red on main 34157ca7: "its script wants item 196, the floor's gate key is 198"): the
// key is what the script compares the window's answer with, not the window's first item. Wise Owl Forest's lock (d02's 161)
// lists all three of the keys its floor build picks from (196, 198, 205), reads the floor's own with _GET_GATEKEY_NO and
// compares the answer with that; the other dungeons' locks list their one key and compare with it as a constant. So: the
// constant at the window's first compare, else the floor's gate key when the script reads it there (what _GET_GATEKEY_NO
// gives retail's script: NowDngMap->gate_key_no), else the window's first item.
// DC_COOP_MUTATE=gatekeyfirst (twin): the window's first item always, as on train 51.
constexpr int kExtGateKeyNo = 1064; // _GET_GATEKEY_NO

int ScriptWantedItem(int script_no) {
    const char     *code = nullptr;
    const vmcode_t *pc = ScriptStart(script_no, &code);
    auto            is_code = [](const vmcode_t *at) { return at->op > 0 && at->op <= RS_OP_COS; };
    auto            item_const = [](const vmcode_t *at) { return at->op == RS_OP_PUSH_CONST && at->arg1 == 1 && at->arg2 >= ITEM_DUNGEON_START; };

    // (Its function's code runs on past a return to the branches after it: read until the code stops being instructions.)
    for (int n = 0; pc != nullptr && n < 1000 && is_code(pc); n++, pc++) {
        if (pc->op != RS_OP_PUSH_CONST || pc->arg1 != 1 || pc->arg2 != kExtItemWindow) {
            continue;
        }

        int first = -1;

        for (const vmcode_t *arg = pc + 1; arg < pc + 4 && arg->op != RS_OP_EXT; arg++) {
            if (item_const(arg)) {
                first = arg->arg2;
                break;
            }
        }

        if (Mutated("gatekeyfirst")) {
            return first;
        }

        bool reads_gate_key = false;

        for (const vmcode_t *at = pc + 1; n < 1000 && is_code(at); n++, at++) {
            if (at->op == RS_OP_PUSH_CONST && at->arg1 == 1 && at->arg2 == kExtGateKeyNo) {
                reads_gate_key = true;
            } else if (at->op == RS_OP_CMP) {
                if (item_const(at - 1)) {
                    return (at - 1)->arg2;
                }
                if (reads_gate_key && NowDngMap != nullptr) {
                    return (int) NowDngMap->gate_key_no;
                }
                break;
            }
        }

        return first;
    }

    return -1;
}

// The key a gate's lock takes: the one its script's window asks for, else (a script that names none) the floor's gate key.
// DC_COOP_MUTATE=gatekeyno (twin): the floor's gate key always, as before.
int LockKey(const CDungeonEvent *event) {
    const int wanted = event != nullptr && !Mutated("gatekeyno") ? ScriptWantedItem(event->script_no) : -1;

    return wanted > 0 ? wanted : (int) NowDngMap->gate_key_no;
}

bool DoorScript(int script_no) {
    return !Mutated("keydoorwide") && DoorScriptCode(script_no);
}

// coop-gateflow (Jep's 2026-11-02 playtest: "If a player uses a key on a door, that should be the end of the prompt. the animation
// of it opening should play and it should remain open for the remainder of the level. The door to join the floor should at least
// reopen when someone joins late too"): the key picker at a gate's lock did the lock's result alone (UseGateKey: the lock off and
// the way out on at once). The gate's doors never moved, and the picker's own Cross, the same tick, was the game's Cross on the
// way out it had just switched on: the exit gate's question came up as the key went in. The lock's program (161 in all seven
// dungeons) is a key door's: its item window, then a call to its doors' opening (two halves slid 15 apart over 60 ticks, sound
// 233), then the lock off and the way out on. It now runs beside the game as a key door does (StartDoorBeside), so the doors
// open, the way out comes on only once they have, and no Cross is shared. DC_COOP_MUTATE=gateinstant (twin): UseGateKey, as
// before.
bool GateDoorScript(int script_no) {
    return !Mutated("gateinstant") && DoorScriptCode(script_no, true);
}

bool DoorOpening(int slot) {
    for (const DoorRun &door : g_doors) {
        if (door.on && door.slot == slot && NowEventMan != nullptr && NowEventMan->event[slot].event == door.event) {
            return true;
        }
    }

    return false;
}

// One step of the door g_who opens: to its next wait, with its handles, the hold button's action mode and its item window
// answered with the pick; the game's own handles and event state put back after. False when it is over: "ended", or why not.
bool StepDoor(DoorRun &door, const char **why) {
    BT_OBJ_HANDLE       game_handles[32];
    const BT_EVENT_INFO game_info = BtEventInfo;
    RS_STACKDATA *const game_result = PortItemSelectResult;

    CCharacter    *const game_main = EdEventInfo.main_character;
    CCameraFollow *const game_camera = EdEventInfo.camera;
    SoundOwner           sound(door.refusing ? g_who : 0); // coop-skeldoor: the refusal's sound is the user's

    std::memcpy(game_handles, BtObjHdl.data(), sizeof game_handles);
    std::memcpy(BtObjHdl.data(), door.handles, sizeof door.handles);
    // coop-skeldoor: the refusal turns and shakes the user, toward the user's camera.
    EdEventInfo.main_character = g_who == 2 ? (CCharacter *) g_partner.chara : &CharaMain;
    EdEventInfo.camera = g_who == 2 && CoopCameraObject() != nullptr ? CoopCameraObject() : NowCamera__3;
    BtEventInfo.action_mode = 2;
    BtEventInfo.request = 0;
    g_door_refused = false;

    if (!door.started) {
        door.started = true;
        door.vm.run(door.event->script_no);
    } else {
        door.vm.resume();
    }

    bool going = door.vm.IsEnd() == 0 && !g_door_refused;

    *why = g_door_refused ? "an ext beyond a door's steps" : door.vm.IsEnd() != 0 ? "ended" : nullptr;

    if (BtEventInfo.request == BT_REQUEST_ITEM_WINDOW) {
        bool listed = false;

        for (int i = 0; i < 13 && BtEventInfo.item_select_list[i] >= ITEM_DUNGEON_START; i++) {
            listed = listed || BtEventInfo.item_select_list[i] == door.item;
        }

        if (BtEventInfo.item_select_filtered && !listed && (door.shown || Mutated("refusenone") || PortItemSelectResult == nullptr)) {
            going = false;
            *why = "not wanted";
        } else if (BtEventInfo.item_select_filtered && !listed) {
            // coop-skeldoor: the window answers with the pick, nothing taken, and the script goes on to its refusal.
            PortItemSelectResult->i = door.item;
            door.refusing = true;
            std::fprintf(stderr, "coop: %s key door at event %s: item %d is not its key: the refusal plays on the user\n",
                        g_who == 1 ? "player 1's" : "partner's", door.event->name, door.item);
        } else {
            if (BtEventInfo.item_select_filtered && !door.shown) {
                TakeOne(PickPack(door.shared), door.item);
            }

            if (BtEventInfo.item_select_result != 0 && PortItemSelectResult != nullptr) {
                PortItemSelectResult->i = door.item;
            }
        }
    }

    std::memcpy(door.handles, BtObjHdl.data(), sizeof door.handles);
    std::memcpy(BtObjHdl.data(), game_handles, sizeof game_handles);
    EdEventInfo.main_character = game_main;
    EdEventInfo.camera = game_camera;
    BtEventInfo = game_info;
    PortItemSelectResult = game_result;
    door.ticks++;
    return going;
}

void EndDoor(DoorRun &door, const char *why) {
    const bool opened = std::strcmp(why, "ended") == 0 && !door.refusing;

    std::fprintf(stderr, "coop: %s key door at event %s %s after %d steps\n", g_who == 1 ? "player 1's" : "partner's",
                door.event != nullptr ? door.event->name : "?", opened ? "opened" : door.refusing ? "refused (its refusal played)" : why,
                door.ticks);
    door.on = false;

    // A door stopped before its script's end (a new floor ends it anyway), or one that refused its pick, is usable again.
    if (door.held && !opened && NowEventMan != nullptr && NowEventMan->event[door.slot].event == door.event) {
        NowEventMan->event[door.slot].Start();
    }
    door.held = false;
    door.refusing = false;
}

// A key door's script, beside the game, with the pick as its answer (see above).
bool StartDoorBeside(const PickEntry &entry) {
    CDungeonEventData &state = NowEventMan->event[Cur().slot];

    if (gameTask != GAME_TASK_PLAY || state.event != Cur().event || !state.enabled || state.hold == 0 || DoorOpening(Cur().slot)) {
        PickNote("Nothing to use it on now.");
        return false;
    }

    if (!Holds(PickPack(entry.shared), entry.item)) {
        PickNote("The item is gone.");
        return false;
    }

    DoorRun              &door = g_doors[g_who - 1];
    const RS_PROG_HEADER *prog = DoorProg();
    const int             exts = std::min(EdEventScript.ext_func_num, kDoorExts);

    for (int i = 0; i < kDoorExts; i++) {
        g_door_exts[i] = i < exts && (DoorExt(i) || (RefusalExt(i) && !Mutated("refusenone"))) ? EdEventScript.ext_func_table[i] : &DoorExtRefused;
    }

    door.on = true;
    door.started = false;
    door.held = false;
    door.refusing = false;
    door.slot = Cur().slot;
    door.event = state.event;
    door.item = entry.item;
    door.shared = entry.shared;
    door.ticks = 0;
    std::memset(door.handles, 0, sizeof door.handles);
    door.vm.ext_func(g_door_exts, kDoorExts);
    door.vm.stack = door.stack;
    door.vm.stack_num = kDoorStack;
    door.vm.stack_end = door.stack + kDoorStack;
    door.vm.call = door.call;
    door.vm.call_num = kDoorCalls;
    door.vm.call_end = door.call + kDoorCalls;
    door.vm.prog = const_cast<RS_PROG_HEADER *>(prog);
    door.vm.code = reinterpret_cast<char *>(door.vm.prog) + prog->code;

    std::printf("coop: %s key picker opens the key door at event %s (script %d) with item %d from %s, beside the game (task %d)\n",
                g_who == 1 ? "player 1's" : "partner's", state.event->name, state.event->script_no, entry.item,
                entry.shared ? "the party's pack" : "her own items", (int) gameTask);

    const char *why = nullptr;

    // Taken: while it opens its slot is off, as retail's script task held every other use of it (both players' Cross on it
    // would start its "locked" message as the game's task, and a picker there is spent). Its script switches it off at its end.
    if (StepDoor(door, &why)) {
        state.Stop();
        door.held = true;
        return true;
    }

    EndDoor(door, why);

    if (std::strcmp(why, "not wanted") == 0) {
        PickNote("Nothing happens.");
        return false;
    }

    return std::strcmp(why, "ended") == 0;
}

// coop-gateflow: a gate opened before this floor was built (the open-gate table, below) or before this copy came onto it (a late
// join: the lock off and the way out on came in the merge's state set, the doors as the floor was built, shut): its lock's script,
// at once, all its steps in this tick, so its doors stand open as the key left them: answered with the floor's key, which is not
// taken, and with no sound (its slide's 233 only quietened). Both copies alike where both build the floor. False when the lock's
// script is not a key door's or does not end.
int DoorExtQuiet(RS_STACKDATA *, int) {
    return 1;
}

bool ShowGateOpen(int slot) {
    static DoorRun quiet;
    static int (*exts_quiet[kDoorExts])(RS_STACKDATA *, int) = {};
    CDungeonEventData    &lock = NowEventMan->event[slot];
    const RS_PROG_HEADER *prog = DoorProg();
    const int             exts = std::min(EdEventScript.ext_func_num, kDoorExts);

    if (prog == nullptr || lock.event == nullptr || !GateDoorScript(lock.event->script_no)) {
        return false;
    }

    for (int i = 0; i < kDoorExts; i++) {
        exts_quiet[i] = i == 807 ? &DoorExtQuiet : i < exts && DoorExt(i) ? EdEventScript.ext_func_table[i] : &DoorExtRefused;
    }

    quiet = DoorRun{};
    quiet.on = true;
    quiet.shown = true;
    quiet.slot = slot;
    quiet.event = lock.event;
    quiet.item = LockKey(lock.event); // coop-gatekeyno
    quiet.shared = true;
    quiet.vm.ext_func(exts_quiet, kDoorExts);
    quiet.vm.stack = quiet.stack;
    quiet.vm.stack_num = kDoorStack;
    quiet.vm.stack_end = quiet.stack + kDoorStack;
    quiet.vm.call = quiet.call;
    quiet.vm.call_num = kDoorCalls;
    quiet.vm.call_end = quiet.call + kDoorCalls;
    quiet.vm.prog = const_cast<RS_PROG_HEADER *>(prog);
    quiet.vm.code = reinterpret_cast<char *>(quiet.vm.prog) + prog->code;

    const int   who = g_who;
    const char *why = nullptr;

    g_who = 1;
    while (StepDoor(quiet, &why) && quiet.ticks < 600) {
    }
    g_who = who;
    quiet.on = false;

    const bool ended = why != nullptr && std::strcmp(why, "ended") == 0;

    std::fprintf(stderr, "coop: gate open: its doors (event %s, script %d) put open at once on this copy (player %d): %s after %d steps\n",
                 lock.event->name, lock.event->script_no, NetplayRole(), ended ? "done" : why != nullptr ? why : "still going", quiet.ticks);
    std::fflush(stderr);
    return ended;
}

// Test hook (DC_COOP_BONEDOOR=T, x-dungeon-p2 -Case bonedoor, x-floor-join -BoneDoor), each tick on a floor, both copies, on the
// press clock (DC_COOP_MENUPRESS's, alike on both linked copies, also after a join's apply; the test's presses use the doors).
// Once per floor, its key doors ("bonedoor: floor ..."). At clock T the floor's first key door (an ndoor* slot whose script
// DoorScript takes) gets its key (DC_COOP_BONEDOOR_KEY, default 216, the Bone Key) in the party's ring and player 1 is put on
// it; at T+50 the partner. Then on every change: "bonedoor: clock C task K pickers P1 P2 door D switch S hold H ring N" (the
// door's slot; N the ring's keys).
// DC_COOP_BONEDOOR_JOIN=DIR (x-floor-join -BoneDoor): the clock is the link's ticks since the last catch-up's lockstep began
// (together, not apart), since the press clock would take the joiner's floor pick Crosses; the presses are the hook's own (his
// Square's picker at T+10, hers at T+60, her Cross on the key at T+90, through the pickers' own calls), and this copy's frames
// go to DIR/p<player>-<clock>.png at T+5, T+40, T+80, T+120 and T+200.
void BoneDoorTest() {
    static const int         at = TestEnvInt("DC_COOP_BONEDOOR", -1);
    static const int         item = TestEnvInt("DC_COOP_BONEDOOR_KEY", 216);
    static const char *const join = TestEnv("DC_COOP_BONEDOOR_JOIN");
    // coop-usebands: DC_COOP_BONEDOOR_LOCK=1: the floor's gate lock (gatekey_N) and its key instead of a key door; his picker alone.
    static const bool        lock = TestEnvOn("DC_COOP_BONEDOOR_LOCK");

    if (at < 0 || NowEventMan == nullptr || NowDngMap == nullptr || UserStatus == nullptr) {
        return;
    }

    static int         floor_said = -1;
    // (coop-warmconvert, WS7: said again at each job on a warm pair, as a fresh copy says it; trace_once_kept twin: once)
    COOP_TEST_TRACE_RESET(floor_said, -1);
    const int          floor_now = selectMapNo * 256 + (int) UserStatus->cur_floor;
    CDungeonEventData *door = nullptr;
    std::string        doors;

    for (CDungeonEventData &data : NowEventMan->event) {
        if (data.event != nullptr && data.enabled && data.event->script_no >= 0) {
            const bool takes = lock ? std::strncmp(data.event->name, "gatekey", 7) == 0 : DoorScriptCode(data.event->script_no);

            door = door == nullptr && takes ? &data : door;
            doors += std::string(doors.empty() ? "" : ", ") + data.event->name + " script " + std::to_string(data.event->script_no) +
                     " hold " + std::to_string(data.hold) + " switch " + std::to_string(data.switch_on) + (takes ? " (a key door)" : "");
        }
    }

    const std::int64_t lockstep = NetplayTestLockstepFrom();
    const std::int64_t clock = join == nullptr ? NetplayTestPressClock()
                               : lockstep >= 0 && !CoopWorldsApart() && NetplayLinkTick() >= lockstep ? NetplayLinkTick() - lockstep : -1;

    // (Once the floor plays: the slots are filled by then.)
    if (floor_now != floor_said && gameTask == GAME_TASK_PLAY && clock >= 0) {
        floor_said = floor_now;
        std::fprintf(stderr, "coop-debug: bonedoor: floor map %d floor %d: %s\n", selectMapNo, (int) UserStatus->cur_floor,
                     doors.empty() ? "no scripted events" : doors.c_str());
    }

    const int          slot = door != nullptr ? (int) (door - NowEventMan->event) : -1;

    const int key = lock ? (int) NowDngMap->gate_key_no : item;

    if (clock == at && door != nullptr) {
        ITEM_PACK &ring = SharedRing();

        for (int i = 0; i < 103; i++) {
            if (ring.item[i] <= 0) {
                ring.item[i] = (std::int16_t) key;
                ring.num = std::max<int>(ring.num, i + 1);
                break;
            }
        }

        CharaMain.SetPosition(door->pos);
        sceVu0CopyVector(CharaMain.pos, door->pos);
        std::fprintf(stderr, "coop-debug: bonedoor: clock %lld: key %d in the ring, player 1 on event %s\n", (long long) clock, key, door->event->name);
    } else if (clock == at + 50 && door != nullptr && !lock) {
        sceVu0CopyVector(g_partner.pos, door->pos);
        std::fprintf(stderr, "coop-debug: bonedoor: clock %lld: partner on event %s\n", (long long) clock, door->event->name);
    }

    if (join != nullptr && door != nullptr) {
        if (clock == at + 10) {
            OpenKeyPickFor(1, door);
        } else if (clock == at + 60 && !lock) {
            OpenKeyPickFor(2, nullptr);
        } else if (clock == at + 90 && KeyPickOpenFor(2)) {
            KeyPick      &hers = g_picks[1];
            InputPadState pad{};

            for (int k = 0; k < hers.count; k++) {
                hers.cursor = hers.items[k].item == item ? k : hers.cursor;
            }
            pad.buttons = kInputCross;
            hers.held = 0;
            StepKeyPickFor(2, pad);
        }

        if (clock == at + 5 || clock == at + 40 || clock == at + 80 || clock == at + 120 || clock == at + 200) {
            static char path[512];

            std::snprintf(path, sizeof path, "%s/p%d-%lld.png", join, NetplayLocalPlayer(), (long long) clock);
            NetplayTestShot(path);
        }
    }

    if (clock < at) {
        return;
    }

    int held = 0;

    for (int i = 0; i < 103; i++) {
        held += SharedRing().item[i] == key ? 1 : 0;
    }

    static std::string last;
    // (coop-warmconvert, WS7: said again at each job on a warm pair, as a fresh copy says it; trace_once_kept twin: kept)
    COOP_TEST_TRACE_RESET(last, std::string());
    char               line[200];
    const int          tracked = slot >= 0 ? slot : g_doors[1].slot;
    const CDungeonEventData *data = tracked >= 0 ? &NowEventMan->event[tracked] : nullptr;

    std::snprintf(line, sizeof line, "task %d pickers %d %d door %d switch %d hold %d ring %d", (int) gameTask, (int) KeyPickOpenFor(1),
                  (int) KeyPickOpenFor(2), (int) (g_doors[0].on || g_doors[1].on), data != nullptr ? (int) data->switch_on : -1,
                  data != nullptr ? (int) data->hold : -1, held);

    if (last != line) {
        std::fprintf(stderr, "coop-debug: bonedoor: clock %lld %s\n", (long long) clock, line);
        last = line;
    }
}

} // namespace

// coop-keydoor: each tick on a floor, both copies: the key doors being opened beside the game go on a step. While another
// task runs (a scene, a chest's presentation) the world holds and the door with it; a new floor ends it.
void KeyDoorTick() {
    BoneDoorTest();

    if (gameTask == GAME_TASK_PLAY) {
        g_her_script_start = false;
    }

    for (int player = 1; player <= 2; player++) {
        DoorRun &door = g_doors[player - 1];

        if (!door.on) {
            continue;
        }

        g_who = player;

        if (NowEventMan == nullptr || door.slot < 0 || NowEventMan->event[door.slot].event != door.event) {
            EndDoor(door, "left behind (a new floor)");
            continue;
        }

        if (gameTask != GAME_TASK_PLAY) {
            continue;
        }

        const char *why = nullptr;
        const bool  going = StepDoor(door, &why);

        // Test lines (DC_COOP_BONEDOOR, x-skel-door): the refusal's pose on the user, every 10 steps.
        if (static const bool traced = TestEnvInt("DC_COOP_BONEDOOR", -1) >= 0; traced && door.refusing && door.ticks % 10 == 0) {
            CCharacter   *body = player == 2 ? (CCharacter *) g_partner.chara : &CharaMain;
            sceVu0FVECTOR rotation;

            body->GetRotation(rotation);
            std::fprintf(stderr, "coop-debug: bonedoor: refusal of player %d step %d motion %d yaw %.2f task %d\n", player, door.ticks,
                         (int) body->motion_no, rotation[1], (int) gameTask);
        }

        if (!going) {
            EndDoor(door, why);
        } else if (door.ticks > 1200) {
            EndDoor(door, "stopped: still going after 1200 steps");
        }
    }
}

// coop-skeldoor (Jep: no player's interaction pauses the other): his Cross at a slot whose Cross only shows a message (a
// key door's "locked" line, a gate's lock) ran its script as the game's one task, which held her. The message shows on his
// screen instead (CrossMessagePrompt(1), drawn on his copy), as hers does since coop-p2prompt. DC_COOP_MUTATE=hiscrosspause
// (twin): the script, as before.
bool HisCrossBeside(int script_no) {
    const bool hers = g_her_script_start;

    g_her_script_start = false;

    if (hers || gameTask != GAME_TASK_SCRIPT_START || BtEventInfo.action_mode != 1 || BtEventInfo.chara_help || Mutated("hiscrosspause") ||
        NowEventMan == nullptr) {
        return false;
    }

    gameTask = GAME_TASK_PLAY; // (what CrossMessagePrompt asks)

    if (!CrossMessagePrompt(1, script_no)) {
        gameTask = GAME_TASK_SCRIPT_START;
        return false;
    }

    return true;
}

bool DoorRefusing(int player) {
    const DoorRun &door = g_doors[player - 1];

    return door.on && door.refusing;
}

bool OpenKeyPick() {
    return OpenKeyPickFor(2, nullptr);
}

bool OpenKeyPickFor(int player, CDungeonEventData *state) {
    // coop-picksound (Jep's 2026-10-28 run: P1 heard her picker close at the cart): the picker's sounds are its player's,
    // heard on that player's copy only. DC_COOP_MUTATE=picksound (twin): nobody's, heard on both, as before.
    SoundOwner sound(Mutated("picksound") ? 0 : player);

    if (Mutated("keypause") || Mutated("keydoor") || gameTask != GAME_TASK_PLAY || BtActStatus.can_act == 0 ||
        BtActStatus.movement_locked != 0 || NowEventMan == nullptr || NowDngMap == nullptr || UserStatus == nullptr) {
        return false;
    }

    // Player 1's steps in MenuTick, so only with the co-op menus on (else his Square runs the window as retail's).
    if (player == 1 && (!MenusOn() || Mutated("keypausep1"))) {
        return false;
    }

    if (player == 2) {
        state = NowEventMan->SearchDataSlotPos2(g_partner.pos);
    }

    if (state == nullptr || state->event->script_no == -1 || state->hold == 0 || state->event->fade != 0 ||
        (player == 2 && state->event->chara_no != -1 && state->chara_done == g_partner.loaded_chara)) {
        return false; // nothing that wants an item: her Square uses her item
    }

    g_who = player;

    // coop-keydoor: a key door already opening beside the game (the other player's key) wants nothing more.
    if (DoorOpening((int) (state - NowEventMan->event))) {
        SndSePlay(MENU_SOUND_REFUSE, -1, 0);
        PickNote("It is opening.");
        return true;
    }

    COOP_TEST_SWITCH(const char *, door, TestEnv("DC_COOP_KEYDOOR")); // (coop-warmconvert WS7 round 4: a job switch)
    ITEM_PACK &party = PickPack(true); // coop-keyring: the ring
    ITEM_PACK         &own = player == 1 ? UserStatus->item_pack : PartnerPack();

    Cur().count = 0;
    Cur().cursor = 0;
    Cur().answer = -1;
    Cur().gate = std::strncmp(state->event->name, "gatekey", 7) == 0 && !(door != nullptr && std::string(door) == "script");

    for (int i = 0; i < party.num && i < 103; i++) {
        int item = party.item[i];

        if (Mutated("keyshared") ? item >= ITEM_DUNGEON_START : SharedItem(item)) {
            AddPick(item, true);
        }
    }

    for (int i = 0; i < own.num && i < 103 && !Mutated("keyshared"); i++) {
        int item = own.item[i];

        if (item >= ITEM_DUNGEON_START && item < ITEM_WEAPON_START && !SharedItem(item)) {
            AddPick(item, &own == &party && player == 2);
        }
    }

    const int key = Cur().gate ? LockKey(state->event) : (int) NowDngMap->gate_key_no; // coop-gatekeyno

    for (int k = 0; k < Cur().count; k++) {
        if (Cur().items[k].item == key) {
            Cur().cursor = k;
        }
    }

    if (Cur().count == 0) {
        SndSePlay(MENU_SOUND_REFUSE, -1, 0);
        PickNote("No item to use here.");
        return true;
    }

    Cur().open = true;
    Cur().slot = (int) (state - NowEventMan->event);
    Cur().event = state->event;
    Cur().held = kInputSquare;
    SndSePlay(MENU_SOUND_CURSOR, -1, 0);

    std::string list;

    for (int k = 0; k < Cur().count; k++) {
        list += (k ? " " : "") + std::to_string(Cur().items[k].item) + (Cur().items[k].shared ? "s" : "");
    }

    std::printf("coop: %s key picker opens at event %s (%d items: %s), the game plays on (task %d)\n",
                player == 1 ? "player 1's" : "partner's", state->event->name, Cur().count, list.c_str(), (int) gameTask);
    // coop-gatekeyno: the lock's key and the floor's gate key, which disagreed in Jep's pack 8 session (the cause still open).
    if (Cur().gate) {
        std::fprintf(stderr, "coop: %s key picker at lock %s: its script wants item %d, the floor's gate key is %d (map %d floor %d, map type %d)\n",
                     player == 1 ? "player 1's" : "partner's", state->event->name, ScriptWantedItem(state->event->script_no),
                     (int) NowDngMap->gate_key_no, selectMapNo, UserStatus != nullptr ? (int) UserStatus->cur_floor : -1, (int) NowDngMap->map_type);
    }
    return true;
}

bool KeyPickOpen() {
    return KeyPickOpenFor(2);
}

bool KeyPickOpenFor(int player) {
    return g_picks[player - 1].open;
}

// coop-doorfix (Jep's 2026-10-30 Local playtest: P2 apart, P1 in the mayor's house, could not open the keyring at the cave
// door): apart, this copy's player is the game's walker, with their own bag in the game's pack and the party's ring beside
// it (her copy: CoopWorldsBagsSwapped), so the ring is SharedRing's wherever it is and the user's own items the game's pack
// (apart, or player 1) or her bag beside it (together). DC_COOP_MUTATE=cavepick_linked_only (twin): the lists as before
// (the game's pack as the party's), and apart no picker at all (CaveDoorSquares).
ITEM_PACK &TownRing(CDngStatusData *status) {
    if (RingOn()) {
        return SharedRing(); // coop-worldkeys: the world's ring, in no bag
    }
    return CoopWorldsBagsSwapped() && !Mutated("cavepick_linked_only") && !Mutated("cavedoor_off") ? g_bag.pack : status->item_pack;
}

ITEM_PACK &TownOwnPack(CDngStatusData *status, int player) {
    return player == 1 || CoopWorldsApart() ? status->item_pack : PartnerPack();
}

bool OpenTownDoorPickFor(int player) {
    SoundOwner sound(Mutated("picksound") ? 0 : player); // coop-picksound

    CDngStatusData *status = SaveData != nullptr ? SaveData->GetDngStatus() : nullptr;

    if (status == nullptr || g_picks[player - 1].open) {
        return false;
    }

    g_who = player;

    // The party's story items and keys (the ring), then the user's own bag's items (separate bags).
    ITEM_PACK &party = TownRing(status);
    ITEM_PACK &own = TownOwnPack(status, player);

    Cur() = KeyPick{};
    Cur().town = true;
    Cur().apart = CoopWorldsApart();

    for (int i = 0; i < party.num && i < 103; i++) {
        if (SharedItem(party.item[i])) {
            AddPick(party.item[i], true);
        }
    }

    for (int i = 0; i < own.num && i < 103; i++) {
        const int item = own.item[i];

        if (item >= ITEM_DUNGEON_START && item < ITEM_WEAPON_START && !SharedItem(item)) {
            AddPick(item, &own == &party && player == 2);
        }
    }

    // (test hook DC_COOP_DOORPROMPT_WRONG=ITEM, x-cave-door wrong: the cursor starts on that item, so the test's Cross picks it)
    static const int wrong = TestEnvInt("DC_COOP_DOORPROMPT_WRONG", 0);

    for (int k = 0; k < Cur().count; k++) {
        if (Cur().items[k].item == (wrong > 0 ? wrong : ITEM_CAVE_KEY)) {
            Cur().cursor = k;
        }
    }

    if (Cur().count == 0) {
        Cur().town = false;
        SndSePlay(MENU_SOUND_REFUSE, -1, 0);
        PickNote("No item to use here.");
        return true;
    }

    Cur().open = true;
    Cur().held = kInputSquare;
    SndSePlay(MENU_SOUND_CURSOR, -1, 0);

    std::string list;

    for (int k = 0; k < Cur().count; k++) {
        list += (k ? " " : "") + std::to_string(Cur().items[k].item) + (Cur().items[k].shared ? "s" : "");
    }

    // (coop-doorfix: on stderr, which a test reads before it stops the copy)
    std::fprintf(stderr, "coop: %s key picker opens at the cave door (%d items: %s), the game plays on\n", player == 1 ? "player 1's" : "partner's",
                 Cur().count, list.c_str());
    return true;
}

bool TownDoorPickOpenFor(int player) {
    return g_picks[player - 1].open && g_picks[player - 1].town;
}

bool TownDoorPickHeldFor(int player) {
    return TownDoorPickOpenFor(player) || g_picks[player - 1].town_closed;
}

int TownCaveKeyUsed() {
    const int user = g_cave_key_user;

    g_cave_key_user = 0;
    return user;
}

bool StepKeyPick(const InputPadState &pad) {
    return StepKeyPickFor(2, pad);
}

bool StepKeyPickFor(int player, const InputPadState &pad) {
    SoundOwner sound(Mutated("picksound") ? 0 : player); // coop-picksound

    g_who = player;
    Cur().town_closed = false;

    std::uint16_t pressed = pad.buttons & ~Cur().held;

    Cur().held = pad.buttons;

    if (Cur().note_ticks > 0) {
        Cur().note_ticks--;
    }

    if (!Cur().open) {
        return false;
    }

    // coop-doorprompt: the town door's picker closes when the town is left or the pair goes apart. coop-doorfix: or comes
    // together (one opened apart is the walker's own on this copy alone).
    const bool apart_changed = Mutated("cavepick_linked_only") || Mutated("cavedoor_off") ? CoopWorldsApart() : CoopWorldsApart() != Cur().apart;

    if (Cur().town && (mode != 2 /* GAME_MODE_EDIT */ || apart_changed)) {
        ClosePick();
        return false;
    }

    // Knocked down, or the floor changed under it: the picker closes.
    if (!Cur().town && ((player == 2 && g_partner.down > 0) || NowEventMan == nullptr || Cur().slot < 0 || NowEventMan->event[Cur().slot].event != Cur().event)) {
        ClosePick();
        return false;
    }

    // coop-keydoor (Jep's 2026-10-27 run: his picker stayed open at the Bone Key door after her key opened it): the other
    // player used the slot (its script switched it off or let go of its hold, or its key door is opening beside the game):
    // the picker closes, as a picker does once used. DC_COOP_MUTATE=keypickstays (twin): it stays open.
    if (!Cur().town && !Mutated("keypickstays")) {
        const CDungeonEventData &data = NowEventMan->event[Cur().slot];

        if (!data.enabled || data.switch_on == 0 || data.hold == 0 || DoorOpening(Cur().slot)) {
            std::printf("coop: %s key picker closes: event %s was used (switch %d, hold %d, opening %d)\n", player == 1 ? "player 1's" : "partner's",
                        data.event->name, (int) data.switch_on, (int) data.hold, (int) DoorOpening(Cur().slot));
            PickNote("Someone used it first.");
            ClosePick();
            return false;
        }
    }

    if ((pressed & (kInputDown | kInputRight)) != 0) {
        Cur().cursor = (Cur().cursor + 1) % Cur().count;
        SndSePlay(MENU_SOUND_CURSOR, -1, 0);
    } else if ((pressed & (kInputUp | kInputLeft)) != 0) {
        Cur().cursor = (Cur().cursor + Cur().count - 1) % Cur().count;
        SndSePlay(MENU_SOUND_CURSOR, -1, 0);
    }

    // coop-dungeonsuite (x-dungeon-suite lock -Other both): the other player's Cross at the same lock ran its script (a game-wide
    // task) just as this picker's Cross came, and UseGateKey refused with "The lock is already open." while the lock stayed shut and
    // the picker closed. While another task runs the pick waits: the picker stays open for the next Cross.
    // DC_COOP_MUTATE=pickbusy: the pick goes ahead and is refused, as before (the twin).
    if ((pressed & kInputCross) != 0 && Cur().gate && gameTask != GAME_TASK_PLAY && !Mutated("pickbusy")) {
        std::printf("coop: %s key picker waits: task %d runs\n", player == 1 ? "player 1's" : "partner's", (int) gameTask);
        PickNote("Not now.");
        return true;
    }

    if ((pressed & kInputCross) != 0) {
        PickEntry entry = Cur().items[Cur().cursor];

        std::printf("coop: %s key picker picks item %d (%s)\n", player == 1 ? "player 1's" : "partner's", entry.item,
                    entry.shared ? "shared" : "her own");

        // coop-doorprompt: the door takes the cave key alone; the town opens the door and sends its player in
        // (TownCaveKeyUsed). coop-sharedkeys (Jep's 2026-10-27 playtest: the key stayed in the bag after the door opened): the
        // key is spent, as the game's own Use Item window at that door spends it (edit.cpp's ED_MENU_USE_ITEM opens it with
        // vanish 1; event 130 itself deletes no item). Both copies on the same tick. DC_COOP_MUTATE=cavekeykeep: kept (twin).
        if (Cur().town) {
            CDngStatusData *status = SaveData != nullptr ? SaveData->GetDngStatus() : nullptr;

            if (entry.item == ITEM_CAVE_KEY) {
                // coop-doorfix: out of the pack it was picked from (the ring apart is beside the game's pack).
                ITEM_PACK *from = status == nullptr ? nullptr : entry.shared ? &TownRing(status) : &TownOwnPack(status, player);
                const bool spent = from != nullptr && !Mutated("cavekeykeep") && TakeOne(*from, ITEM_CAVE_KEY);

                g_cave_key_user = player;
                SndSePlay(MENU_SOUND_CONFIRM, -1, 0);
                std::fprintf(stderr, "coop: %s uses the cave key at the cave door (%s), the game plays on\n", player == 1 ? "player 1" : "partner",
                            spent ? "spent" : "kept");
            } else if (Mutated("cavenote") || Mutated("cavedoor_off")) {
                SndSePlay(MENU_SOUND_REFUSE, -1, 0);
                PickNote("Nothing happens.");
            } else {
                // coop-doorfix (Jep's 2026-10-30 Local playtest: a wrong item gave "a window cancelled sound" and a "Nothing
                // happens." of ours): as retail's window, a pick it does not want is confirmed and nothing taken, and the door's
                // event plays its refusal on the user (coop_town's CoopTownCaveRefusal, both copies), no message.
                SndSePlay(MENU_SOUND_CONFIRM, -1, 0);
                std::fprintf(stderr, "coop: %s key picker: item %d is not the cave key: confirmed, nothing taken, the door refuses it\n",
                             player == 1 ? "player 1's" : "partner's", entry.item);
                ClosePick();
                CoopTownCaveRefusal(player);
                return true;
            }
        } else if (OtherFloorsOnly(PickPack(entry.shared), entry.item)) {
            SndSePlay(MENU_SOUND_REFUSE, -1, 0);
        } else if (!Cur().gate) {
            if (!(DoorScript(Cur().event->script_no) ? StartDoorBeside(entry) : StartWithAnswer(entry))) { // coop-keydoor
                SndSePlay(MENU_SOUND_REFUSE, -1, 0);
            }
        } else if (GateDoorScript(Cur().event->script_no) && !Mutated("gatekeyno")) {
            // coop-gateflow: the lock's own script, its doors opening beside the game. coop-gatekeyno: whatever the pick, as
            // retail's window answers its script: the script takes its key and plays its refusal on the user for anything else.
            if (!StartDoorBeside(entry)) {
                SndSePlay(MENU_SOUND_REFUSE, -1, 0);
            }
        } else if (entry.item != LockKey(Cur().event)) { // coop-gatekeyno: the lock's own key
            SndSePlay(MENU_SOUND_REFUSE, -1, 0);
            PickNote("Nothing happens.");
        } else if (GateDoorScript(Cur().event->script_no)) {
            if (!StartDoorBeside(entry)) {
                SndSePlay(MENU_SOUND_REFUSE, -1, 0);
            }
        } else if (!UseGateKey(entry)) {
            SndSePlay(MENU_SOUND_REFUSE, -1, 0);
        }

        ClosePick();
        return true;
    }

    if ((pressed & (kInputCircle | kInputTriangle)) != 0) {
        SndSePlay(MENU_SOUND_REFUSE, -1, 0);
        std::printf("coop: %s key picker closed\n", player == 1 ? "player 1's" : "partner's");
        ClosePick();
    }

    return true;
}

// coop-gatekeyno (coop_interact.hpp): at each floor's start, the floor's gate key against the key its lock's script asks for, when
// they differ (Jep's pack 8 session had them differ on every build of the cave's floor 4; the next log names when).
void LogFloorGateKey() {
    if (NowEventMan == nullptr || NowDngMap == nullptr) {
        return;
    }

    for (const CDungeonEventData &data : NowEventMan->event) {
        if (data.event != nullptr && data.enabled && std::strncmp(data.event->name, "gatekey", 7) == 0) {
            const int wanted = ScriptWantedItem(data.event->script_no);

            if (wanted > 0 && wanted != NowDngMap->gate_key_no) {
                std::fprintf(stderr, "coop: floor's gate key %d is not its lock's (%s, script %d, wants %d): map %d floor %d, map type %d, %s map\n",
                             (int) NowDngMap->gate_key_no, data.event->name, data.event->script_no, wanted, selectMapNo,
                             UserStatus != nullptr ? (int) UserStatus->cur_floor : -1, (int) NowDngMap->map_type,
                             NowDngMap == &UraDungeonMap ? "the back" : "the main");
                std::fflush(stderr);
            }
            return;
        }
    }
}

bool KeyPickAnswer() {
    g_who = g_picks[0].answer >= 0 ? 1 : 2;

    if (Cur().answer < 0) {
        return false;
    }

    int  item = Cur().answer;
    bool accepted = false;

    Cur().answer = -1;

    // As the window's Cross: an item in the script's list is taken (when the script asks it to vanish); any other
    // is still the answer (the script's own "nothing happens").
    for (int i = 0; i < 13 && BtEventInfo.item_select_list[i] >= ITEM_DUNGEON_START; i++) {
        accepted = accepted || BtEventInfo.item_select_list[i] == item;
    }

    if (accepted && BtEventInfo.item_select_filtered) {
        TakeOne(PickPack(Cur().answer_shared), item);
    }

    if (BtEventInfo.item_select_result != 0) {
        PortItemSelectResult->i = item;
    }

    BtEventInfo.item_select_result = 0;
    std::printf("coop: %s pick %d answers the window (%s, %s), no window opens\n", g_who == 1 ? "player 1's" : "partner's", item,
                accepted ? "accepted" : "not wanted", Cur().answer_shared || g_who == 1 ? "the party's pack" : "her own items");
    return true;
}

void KeyPickResetForSession() {
    g_picks[0] = KeyPick{};
    g_picks[1] = KeyPick{};
    g_cave_key_user = 0;
    for (DoorRun &door : g_doors) {
        door.on = false; // coop-keydoor
    }
}

// coop-gateopen (Jep, 2026-10-27): a gate either player opens with its key stays open for both. The key is spent; the
// other player walks up to the open gate and gets the way out's own "go down?" and no key prompt. Together the copies
// open it on one tick (lockstep); the gap was a floor the other copy builds anew: he opens floor 2's gate and goes on,
// she reaches floor 2 later and found it locked, its key spent and its key monster's drop to win again. So each dungeon
// floor whose gate's way out is on goes in a table (dungeon, floor), the same on both copies: lockstep sets it alike
// together, an apart copy tells the other (CoopWorldsGateOpened, kWhatGate progress) and takes the other's
// (GateOpenFromPeer), and a merge's snapshot carries it (OpenGatesBytes). Any floor in the table has its gate opened as
// soon as it is built (its lock off, its way out on, as UseGateKey and the lock's own script leave them). The table
// lasts the session (a floor built again keeps its gate open: the retail game would want a new key). Linked pairs only.
// DC_COOP_MUTATE=gateopen (twin): no table, as before.
namespace {

struct OpenGates {
    std::uint32_t bits[8][4]; // [dungeon][floor / 32], floors 0-127
};

OpenGates g_gates{};

bool GateSlot(int dungeon, int floor) {
    return dungeon >= 0 && dungeon < 8 && floor >= 0 && floor < 128;
}

bool GateOpenIn(int dungeon, int floor) {
    return GateSlot(dungeon, floor) && (g_gates.bits[dungeon][floor >> 5] & (1u << (floor & 31))) != 0;
}

// True when it was not in the table yet.
bool MarkGateOpen(int dungeon, int floor) {
    if (!GateSlot(dungeon, floor) || GateOpenIn(dungeon, floor)) {
        return false;
    }

    g_gates.bits[dungeon][floor >> 5] |= 1u << (floor & 31);
    return true;
}

} // namespace

void GateOpenTick() {
    // coop-gateflow (DC_COOP_DEBUG): once a floor build, what this tick sees, for x-gate-flow.
    if (static int said = -1; CoopDebug() && mode == 3 /* GAME_MODE_DUNGEON */ && said != NetplayFloorBuilds() && UserStatus != nullptr) {
        said = NetplayFloorBuilds();
        std::fprintf(stderr, "coop-debug: gate open: watching build %d, here floor %d, role %d, dungeon %d floor %d\n", said, CoopWorldsHereFloor(),
                     NetplayRole(), selectMapNo, (int) UserStatus->cur_floor);
    }

    if (Mutated("gateopen") || mode != 3 /* GAME_MODE_DUNGEON */ || UserStatus == nullptr || selectMapNo < 0) {
        return;
    }

    CDungeonEventData *lock = nullptr;
    CDungeonEventData *way_out = nullptr;

    for (CDungeonEventData &data : DngEventMan.event) {
        if (lock == nullptr && data.event != nullptr && data.enabled && std::strncmp(data.event->name, "gatekey", 7) == 0) {
            lock = &data;
        }
    }

    const char *suffix = lock != nullptr ? std::strchr(lock->event->name, '_') : nullptr;

    for (CDungeonEventData &data : DngEventMan.event) {
        const char *own = data.event != nullptr ? std::strchr(data.event->name, '_') : nullptr;

        if (way_out == nullptr && suffix != nullptr && data.event != nullptr && data.enabled && std::strncmp(data.event->name, "out", 3) == 0 &&
            own != nullptr && std::strcmp(own, suffix) == 0) {
            way_out = &data;
        }
    }

    if (lock == nullptr || way_out == nullptr) {
        return; // a floor with no gate (a boss's)
    }

    // coop-gateflow: a gate open on a floor this copy built after it was opened (the table, below, or a late join's state set;
    // on any copy, its place in the worlds aside) has its doors put open too, once a build (the floor's own opening leaves
    // them so already; again is the same). Not while a lock's script opens them. DC_COOP_MUTATE=gateshut (twin): the doors
    // stay as built, shut.
    static int shown_build = -1;
    // coop-warmreset (WS7): again at a warm pair's job (its fixture on the same build left the doors as the state set has them);
    // DC_COOP_MUTATE=job_gate_kept (twin): not, as before.
    COOP_TEST_JOB_RESET(shown_build, -1, "job_gate_kept");
    const int  lock_slot = (int) (lock - DngEventMan.event);

    if (NowEventMan == &DngEventMan && lock->switch_on == 0 && way_out->switch_on != 0 && shown_build != NetplayFloorBuilds() &&
        !DoorOpening(lock_slot) && !Mutated("gateshut")) {
        shown_build = NetplayFloorBuilds();
        ShowGateOpen(lock_slot);
    }

    if (NetplayRole() == 0 || CoopWorldsHereFloor() < 0) {
        return;
    }

    const int dungeon = selectMapNo;
    const int floor = UserStatus->cur_floor;

    // The floor whose lock this copy saw locked: only its opening counts, not the old floor's slots still there for a
    // moment after the way out moved cur_floor on (they would mark the next floor).
    static int armed = -1;

    if (lock->switch_on != 0) {
        armed = dungeon * 256 + floor;
    }

    if (lock->switch_on == 0 && way_out->switch_on != 0) {
        if (armed == dungeon * 256 + floor && MarkGateOpen(dungeon, floor)) {
            std::fprintf(stderr, "coop: gate open: dungeon %d floor %d opened on this copy (player %d), %s\n", dungeon, floor, NetplayRole(),
                         CoopWorldsApart() ? "apart: told to the other copy" : "together");
            std::fflush(stderr);
            CoopWorldsGateOpened(dungeon, floor);
        }
    } else if (GateOpenIn(dungeon, floor) && lock->switch_on != 0) {
        lock->Stop();
        way_out->Start();
        std::fprintf(stderr, "coop: gate open: dungeon %d floor %d was opened before, its gate opens here (event %s, way out %s)\n", dungeon,
                     floor, lock->event->name, way_out->event->name);
        std::fflush(stderr);
    }
}

void GateOpenFromPeer(int dungeon, int floor) {
    if (!Mutated("gateopen") && MarkGateOpen(dungeon, floor)) {
        std::fprintf(stderr, "coop: gate open: dungeon %d floor %d opened on the other copy\n", dungeon, floor);
        std::fflush(stderr);
    }
}

bool GateOpenKnown(int dungeon, int floor) {
    return GateOpenIn(dungeon, floor);
}

unsigned char *OpenGatesBytes(std::size_t *size) {
    *size = sizeof g_gates;
    return reinterpret_cast<unsigned char *>(&g_gates);
}

// Test hook (DC_COOP_KEYDOOR=1, with DC_COOP_PLACE=gatekey): once on the floor, the floor's gate key falls at the
// partner's feet, so only she picks it up; then each change of the gate lock's hold and of the party's count of
// the key is logged, for x-dungeon-p2 keydoor. Both copies do the same on the same tick (lockstep).
// coop-warmconvert (WS7 round 4, x-dungeon-p2's keydoor rows as warm jobs): the switch is a job's own and its counts and said
// states start again at each job (from the job's fixture, as a fresh copy's from its load). DC_COOP_MUTATE=keydoor_job_kept
// (warm-repeat-selftest -Group convert -CoopTwin keydoor_job_kept -Only keydoor): kept from the job before, as before.
void KeyDoorTest() {
    COOP_TEST_SWITCH(bool, on, TestEnv("DC_COOP_KEYDOOR") != nullptr);
    static int ticks = 0;
    static int hold = -2;
    static int held = -2;
    static bool p1_put = false;
    static int  seen[96][2];
    static int  bread = -1;
    static int  open_for = 0;
    static int  job = TestJobGeneration();

    if (job != TestJobGeneration()) {
        job = TestJobGeneration();
        if (!TestJobResetKept("keydoor_job_kept")) {
            ticks = 0;
            hold = held = -2;
            p1_put = false;
            std::memset(seen, 0, sizeof seen);
            bread = -1;
            open_for = 0;
        }
    }

    if (!on || NowDngMap == nullptr || NowEventMan == nullptr || UserStatus == nullptr || RandomItem == nullptr) {
        return;
    }

    int key = NowDngMap->gate_key_no;

    if (++ticks == 2) {
        InventoryTestSeed(); // coop-keypick: DC_COOP_BAGSEED (her own Bread)
    }

    // coop-keyring: DC_COOP_KEYDOOR=p1pick: at Toan's feet instead, so player 1 picks it up (his game's own pickup and its
    // presentation) and her Square and Cross at the lock use it (x-dungeon-p2 keydoorp1).
    COOP_TEST_SWITCH(bool, p1pick, std::string(TestEnvOr("DC_COOP_KEYDOOR", "")) == "p1pick");

    if (ticks == 30) {
        sceVu0FVECTOR at;

        sceVu0CopyVector(at, p1pick ? CharaMain.pos : g_partner.pos);
        RandomItem->Set(at, 101, -1, key);
        std::printf("coop-debug: keydoor: gate key %d dropped at the %s's feet, player %.0f away\n", key, p1pick ? "player" : "partner",
                    DistVector(g_partner.pos, CharaMain.pos));
    }

    int now_held = 0;

    for (int i = 0; i < 103; i++) { // GetItem fills the first free of all 103 slots
        if (SharedRing().item[i] == key) { // coop-keyring: the ring wherever this copy keeps it
            now_held++;
        }
    }

    int now_hold = -1;

    for (CDungeonEventData &data : NowEventMan->event) {
        if (data.event != nullptr && data.enabled && std::strncmp(data.event->name, "gatekey", 7) == 0) {
            now_hold = data.hold;

            // DC_COOP_KEYDOOR=p1: once her pickup is in the party's pack and its presentation is over (tick 450), Toan stands on the lock
            // too, so player 1's own Square uses the key she picked up (the ring is either player's).
            if (!p1_put && now_held > 0 && ticks >= 450 && gameTask == GAME_TASK_PLAY && std::string(TestEnvOr("DC_COOP_KEYDOOR", "")) == "p1") {
                p1_put = true;
                CharaMain.SetPosition(data.pos);
                sceVu0CopyVector(CharaMain.pos, data.pos);
                std::printf("coop-debug: keydoor: tick %d player 1 put on event %s\n", ticks, data.event->name);
            }

            break;
        }
    }

    // Every event's hold and switch, so the test sees which one the key's script releases.

    for (int i = 0; i < 96; i++) {
        CDungeonEventData &data = NowEventMan->event[i];
        int                hold_now = data.event != nullptr && data.enabled ? data.hold : -1;
        int                switch_now = data.event != nullptr && data.enabled ? data.switch_on : -1;

        if (ticks > 1 && (seen[i][0] != hold_now || seen[i][1] != switch_now)) {
            std::printf("coop-debug: keydoor: tick %d event %d %s hold %d -> %d, switch %d -> %d\n", ticks, i,
                        data.event != nullptr ? data.event->name : "-", seen[i][0], hold_now, seen[i][1], switch_now);
        }

        seen[i][0] = hold_now;
        seen[i][1] = switch_now;
    }

    if (now_hold != hold || now_held != held) {
        std::printf("coop-debug: keydoor: tick %d gate lock hold %d, party holds key %d x%d, task %d\n", ticks, now_hold, key,
                    now_held, (int) gameTask);
        hold = now_hold;
        held = now_held;
    }

    // coop-keypick: while her picker is open, the game's task and where Toan stands, every 20 ticks (x-dungeon-p2
    // keydoor: the task stays play and player 1's stick moves him).
    // Her own Bread (DC_COOP_BAGSEED=/148), so keydooritem sees it stay in her bag.
    int        bread_now = 0;
    ITEM_PACK &own = PartnerPack();

    for (int i = 0; i < own.num && i < 103; i++) {
        bread_now += own.item[i] == ITEM_BREAD ? 1 : 0;
    }

    if (bread_now != bread) {
        std::printf("coop-debug: keydoor: tick %d her bag carries %d of item %d\n", ticks, bread_now, (int) ITEM_BREAD);
        bread = bread_now;
    }

    int owner = KeyPickOpenFor(1) ? 1 : KeyPickOpenFor(2) ? 2 : 0;

    open_for = owner != 0 ? open_for + 1 : 0;

    if (open_for % 20 == 1) {
        std::printf("coop-debug: keydoor: tick %d picker open, task %d, player at (%.1f %.1f), partner at (%.1f %.1f), owner %d\n", ticks,
                    (int) gameTask, CharaMain.pos[0], CharaMain.pos[2], g_partner.pos[0], g_partner.pos[2], owner);
    }
}

// Test hook (DC_COOP_KEYRING), each dungeon tick (apart too, where KeyDoorTest does not run): while this copy is apart, its
// own player uses the floor's gate key from the party's ring at the gate's lock, as player 1's Square and Cross do (his own
// picker, OpenKeyPickFor(1)):
//   stay: after 300 ticks of apart play (her copy, left on the floor with the key she picked up while together).
//   host: the floor's gate key falls at the player's feet after 300 ticks of apart play (the pickup and its presentation are
//         the game's), and he uses it 300 ticks later.
//   watch: the lines below only.
//   drop: (coop-keyfloor, her copy left on the floor) the floor's gate key falls at the player's feet after 300 ticks of
//         apart play; she uses it at the lock after 1500, or (DC_COOP_KEYRING_GO=<file>) once that file is there: the test
//         makes it when the other copy has logged its own try ("after the try"), so her use never comes first, whatever the
//         load (coop-keyhide: on the dun1-f1 fixture the two met at 1500).
//   try:  (coop-keyfloor, his copy on the next floor) after 900 ticks of apart play, the ring's key (found on her floor) at
//         this floor's lock.
// DC_COOP_GATEKEY=N: every floor's gate wants item N, as in the six dungeons with one gate key (the test's Wise Owl
// Forest picks one of three per floor).
// lines_only: from the link's tick (CoopWorldsTick), which runs on a floor where the co-op step may not (after a join).
// Every 300 calls and at each change, on stderr: "keyring: tick T lock switch S, way out switch W, ring holds key K xN,
// apart A".
void KeyRingTest(bool lines_only) {
    static const char *how = TestEnv("DC_COOP_KEYRING");
    static int         ticks = 0;
    static int         play = 0;
    static int         step = 0; // 0 waiting, 1 picker open, 2 picked
    static bool        dropped = false;
    static std::string last;

    if (how == nullptr || NowDngMap == nullptr || NowEventMan == nullptr || UserStatus == nullptr || RandomItem == nullptr) {
        return;
    }

    static const int gate_key = TestEnvInt("DC_COOP_GATEKEY", -1);

    if (gate_key > 0 && NowDngMap->gate_key_no != gate_key) {
        std::fprintf(stderr, "coop-debug: keyring: floor's gate key %d made %d (DC_COOP_GATEKEY)\n", NowDngMap->gate_key_no, gate_key);
        NowDngMap->gate_key_no = gate_key;
    }

    const std::string  kind = how;
    const bool         host = kind == "host" || kind == "drop";
    const int          key = NowDngMap->gate_key_no;
    CDungeonEventData *lock = nullptr;
    CDungeonEventData *way_out = nullptr;

    for (CDungeonEventData &data : NowEventMan->event) {
        if (lock == nullptr && data.event != nullptr && data.enabled && std::strncmp(data.event->name, "gatekey", 7) == 0) {
            lock = &data;
        }
    }

    const char *suffix = lock != nullptr ? std::strchr(lock->event->name, '_') : nullptr;

    for (CDungeonEventData &data : NowEventMan->event) {
        const char *own = data.event != nullptr ? std::strchr(data.event->name, '_') : nullptr;

        if (way_out == nullptr && suffix != nullptr && data.event != nullptr && std::strncmp(data.event->name, "out", 3) == 0 && own != nullptr &&
            std::strcmp(own, suffix) == 0) {
            way_out = &data;
        }
    }

    int held = 0;

    for (int i = 0; i < 103; i++) {
        held += SharedRing().item[i] == key ? 1 : 0;
    }

    char line[160];

    std::snprintf(line, sizeof line, "lock switch %d, way out switch %d, ring holds key %d x%d, apart %d", lock != nullptr ? (int) lock->switch_on : -1,
                  way_out != nullptr ? (int) way_out->switch_on : -1, key, held, (int) CoopWorldsApart());

    if (ticks++ % 300 == 0 || last != line) {
        std::fprintf(stderr, "coop-debug: keyring: tick %d %s\n", ticks, line);
        last = line;
    }

    // watch: the lines alone (the copy that joins, x-floor-join late -KeyDoor).
    if (lines_only || !(host || kind == "stay" || kind == "try") || !CoopWorldsApart() || gameTask != GAME_TASK_PLAY || BtEventMode != 0) {
        return;
    }

    play++;

    static int drop_slot = -1;
    static int drop_play = 0;

    if (host && !dropped && play >= 300) {
        dropped = true;
        drop_slot = RandomItem->CheckID();
        drop_play = play;
        RandomItem->Set(CharaMain.pos, 101, -1, key);
        std::fprintf(stderr, "coop-debug: keyring: gate key %d dropped at the player's feet\n", key);
    }

    // Where the player and the dropped key are for a while after the drop (the pickup takes the key only once it has risen,
    // with the player within 5 of it).
    if (dropped && drop_slot >= 0 && play - drop_play <= 150 && (play - drop_play) % 10 == 0) {
        const int i = drop_slot;
        const float to_lock = lock != nullptr ? std::hypot(lock->pos[0] - CharaMain.pos[0], lock->pos[2] - CharaMain.pos[2]) : -1.0f;

        std::fprintf(stderr, "coop-debug: keyring: drop +%d: player at (%.0f %.0f), key id %d state %d distance %.1f blocked %d, motion %d, lock %.0f away, event mode %d task %d\n",
                     play - drop_play, CharaMain.pos[0], CharaMain.pos[2], RandomItem->id[i], RandomItem->state[i], RandomItem->distance[i],
                     RandomItem->pickup_blocked[i], (int) CharaMain.motion_no, to_lock, (int) BtEventMode, (int) gameTask);
    }

    // Where the player stood before the lock: he goes back there after his pick, so the test's later Crosses (a joiner's
    // floor questions) do not take him through the opened gate.
    static sceVu0FVECTOR stood;

    const int use_at = kind == "drop" ? 1500 : kind == "try" ? 900 : host ? 600 : 300;
    // drop with DC_COOP_KEYRING_GO: the test's go file (made once the other copy has tried the key) stands in for the 1500,
    // looked for every 30 ticks.
    static const char *go = TestEnv("DC_COOP_KEYRING_GO");
    static bool        gone = false;

    if (kind == "drop" && go != nullptr && !gone && step == 0 && play % 30 == 0) {
        std::error_code missing;

        gone = std::filesystem::exists(go, missing);
        if (gone) {
            std::fprintf(stderr, "coop-debug: keyring: go file there at apart play %d: she uses the key now\n", play);
        }
    }

    const bool due = kind == "drop" && go != nullptr ? gone : play >= use_at;

    if (step == 0 && due && held > 0 && lock != nullptr && lock->switch_on != 0) {
        sceVu0CopyVector(stood, CharaMain.pos);
        CharaMain.SetPosition(lock->pos);
        sceVu0CopyVector(CharaMain.pos, lock->pos);
        step = OpenKeyPickFor(1, lock) && KeyPickOpenFor(1) ? 1 : 0;
        std::fprintf(stderr, "coop-debug: keyring: player (player %d's copy) put on event %s, picker %s\n", NetplayRole(), lock->event->name,
                     step == 1 ? "open" : "not open");
    } else if (step == 1) {
        InputPadState pad{};

        pad.buttons = kInputCross;
        StepKeyPickFor(1, pad);
        step = 2;

        // The lock and the ring at the try itself (coop-keyhide: on a state fixture the other copy's own use of the key could
        // land before this copy's next keyring line, which then showed the ring without it). keyfloor's test makes her go file
        // on this line.
        int after = 0;

        for (int i = 0; i < 103; i++) {
            after += SharedRing().item[i] == key ? 1 : 0;
        }

        std::fprintf(stderr, "coop-debug: keyring: after the try: lock switch %d, ring holds key %d x%d\n", lock != nullptr ? (int) lock->switch_on : -1, key,
                     after);
    } else if (step == 2) {
        CharaMain.SetPosition(stood);
        sceVu0CopyVector(CharaMain.pos, stood);
        step = 3;
    }
}

// Each player's picker, on that player's copy only (the other linked copy steps it and never draws it; couch draws
// both, his on the left), and its last line for a while after. DC_COOP_MUTATE=keypickboth draws them on both copies.
// coop-p2keygui (Jep's 2026-10-27 playtest: "The shared keyring shouldn't lose the built in GUI for using items"): the picker
// is drawn as the game's own Use Item window (InitEventItemSelect's board, from commenu's eventmnu2.pak, the item icons, the
// cursor frame and hand of the stay-frame sheet), with the picker's list (the shared keys first, then her bag) in its cells,
// five to a row, two rows in view, scrolled with the cursor. Draw-only, on the picker owner's copy: its pictures load there
// at its first draw into a block of its own (kKeyWindowBlock; the game's window takes 0x18 or 16). False while they are not
// there (the text list then). DC_COOP_MUTATE=keypicktext (twin): the text list, as before.
constexpr int kKeyWindowBlock = 0x45;
short        *g_key_window_messages = nullptr; // coop-usebands2: eventmnu2.pak's eventuse.bin, the box's item messages

short *KeyWindowMessages() {
    return g_key_window_messages;
}

CTexture *KeyWindowTexture(const char *name) {
    char copy[32];

    std::snprintf(copy, sizeof copy, "%s", name);
    return TexManager.GetTexture(copy, kKeyWindowBlock);
}

bool LoadKeyWindow() {
    static std::vector<u_long128> board;
    static std::vector<u_long128> icons;
    static int                    tries = 0; // failed loads (coop-keydoor: a join's apply drops the block, which loads again)

    if (KeyWindowTexture("eventmnu") != nullptr && KeyWindowTexture("itemicon") != nullptr) {
        return true;
    }

    if (tries >= 3) {
        return false;
    }

    board.assign(512 * 1024 / sizeof(u_long128), u_long128{});
    icons.assign(384 * 1024 / sizeof(u_long128), u_long128{});

    char   board_name[] = "eventmnu2.pak";
    char   icons_name[] = "itemlst.img";
    char   board_tex[] = "eventmnu.img";
    int    board_size = LoadFileMenuData(board_name, reinterpret_cast<unsigned int *>(board.data()));
    int    icons_size = LoadFileMenuData(icons_name, reinterpret_cast<unsigned int *>(icons.data()));
    u_int *sheet = board_size > 0 ? GetPackFile(reinterpret_cast<u_int *>(board.data()), board_tex, nullptr) : nullptr;

    if (sheet == nullptr || icons_size <= 0 || board_size > (int) (board.size() * sizeof(u_long128)) ||
        icons_size > (int) (icons.size() * sizeof(u_long128))) {
        tries++;
        std::fprintf(stderr, "coop: key picker window: cannot load its pictures (board %d, icons %d bytes)\n", board_size, icons_size);
        return false;
    }

    LOADTEXTURE_INFO2 table[3] = {
        {(char *) sheet,        kKeyWindowBlock, 0},
        {(char *) icons.data(), kKeyWindowBlock, 0},
        {nullptr,               0,               0},
    };

    char messages_name[] = "eventuse.bin";

    g_key_window_messages = reinterpret_cast<short *>(GetPackFile(reinterpret_cast<u_int *>(board.data()), messages_name, nullptr));
    TexManager.DeleteTextureBlock(kKeyWindowBlock);
    TexManager.CleanUpTextureList();
    TexManager.LoadTextureBlockEX(kKeyWindowBlock, table);
    std::fprintf(stderr, "coop: key picker window: pictures loaded (board %d, icons %d bytes)\n", board_size, icons_size);
    return KeyWindowTexture("eventmnu") != nullptr && KeyWindowTexture("itemicon") != nullptr;
}

// One column of the board: its top part, a middle stretched by the language's extra height, its foot (the sheet's own rows).
void KeyWindowColumn(CTexture *board, int x, int y, int width, int u, int u_width, int extra, int alpha) {
    DrawMenu2DSprite(board, CRect_i_(x, y, width, 0x94), CRect_i_(u, 0, u_width, 0x94), alpha);
    DrawMenu2DSprite(board, CRect_i_(x, y + 0x94, width, extra + 0x32), CRect_i_(u, 0x94, u_width, 0x14), alpha);
    DrawMenu2DSprite(board, CRect_i_(x, y + 0x94 + extra + 0x32, width, 0x1E), CRect_i_(u, 0xC6, u_width, 0x1E), alpha);
}

bool DrawKeyWindow(const KeyPick &pick, int player, int local) {
    if (Mutated("keypicktext") || pick.count <= 0 || !LoadKeyWindow()) {
        return false;
    }

    CTexture     *board = KeyWindowTexture("eventmnu");
    CTexture     *icons = KeyWindowTexture("itemicon");

    // coop-usebands (Jep's 2026-10-28 Steam run: the window drew with see-through bands and another item's picture in a gap,
    // not reproduced by a test): each picture's key in the port's registry should still be its own image; when it is not
    // (another image took the key, or none is there), a line says so, once per change, for the next playtest's log.
    for (CTexture *texture : {board, icons}) {
        static unsigned said[2] = {};
        const int       which = texture == board ? 0 : 1;
        const unsigned  tbp = reinterpret_cast<const sceGsTex0 *>(&texture->tex0)->TBP0;
        const dc::render::PortTextureRef ref = dc::render::PortTextureFromCTexture(texture);
        const bool      own = ref.valid && ref.width == (unsigned) texture->width && ref.height == (unsigned) texture->height;
        const unsigned  state = own ? 0u : (tbp | 0x80000000u);

        if (state != said[which]) {
            said[which] = state;
            std::fprintf(stderr, "coop: key picker window: picture %s at key %#x %s (registry %ux%u valid %d, picture %dx%d)\n", texture->name,
                         tbp, own ? "is its own again" : "is NOT its own image", ref.width, ref.height, (int) ref.valid, (int) texture->width,
                         (int) texture->height);
        }
    }

    char          stay_name[] = "stayframe";
    CTexture     *stay = TexManager.GetTexture(stay_name, -1);
    const int     lang = GetMenuLangFlag();
    const int     extra_height = lang > 0 ? 16 : 0;
    const int     extra_width = lang > 0 ? 10 : 0;
    const int     edge = extra_width + 6;
    constexpr int alpha = 0x80;
    // The game's place (180, 210); side by side when one copy shows both pickers (couch).
    const int left = local == 0 ? (player == 1 ? 20 : 340) : 180;
    // coop-usebands2 (Jep, 2026-11-01: ours sat higher than the game's own window): InitEventItemSelect puts the PAL board 20
    // lower (the port is the PAL release). DC_COOP_MUTATE=keywinpal (twin): 210, as before.
#ifdef PAL
    const int pal_y = Mutated("keywinpal") ? 0 : 20;
#else
    const int pal_y = 0;
#endif
    const int top = 210 + (lang > 0 ? -16 : 0) + pal_y;
    const int cells_top = top + 56;
    const int cells_bottom = top + 136;
    const int first_row = std::max(0, pick.cursor / 5 - 1);

    setbilinear(0);

    // The cells, two rows in view.
    for (int row = 0; row < 2; row++) {
        for (int col = 0; col < 5; col++) {
            DrawMenu2DSprite(board, CRect_i_(left + 28 + col * 0x2A, cells_top + row * 0x28 + 1, 0x28, 0x28), CRect_i_(0x116, 0, 0x28, 0x28),
                             alpha);
        }
    }

    // Their items, with the game's drop shadow.
    for (int i = first_row * 5; i < pick.count && i < (first_row + 2) * 5; i++) {
        COM_ITEM_INFO *info = GetCommonItemInfo(pick.items[i].item);

        if (info == nullptr || info->icon_index < 0) {
            continue;
        }

        const int      x = left + 32 + (i % 5) * 0x2A;
        const int      y = cells_top + (i / 5 - first_row) * 0x28 + 4;
        const CRect_i_ source((info->icon_index % 8) * 32, (info->icon_index / 8) * 32, 32, 32);

        set2DSprite(GetVif1Packet(), icons, CRect_i_(x + 2, y + 1, 32, 32), source, 0, 0, 0, alpha * 0x50 >> 7);
        DrawMenu2DSprite(icons, CRect_i_(x, y, 32, 32), source, alpha);
    }

    // The board round them.
    if (Mutated("usebands")) { // coop-usebands (x-floor-join -GateLock's twin): the board's middle with a band left out
        KeyWindowColumn(board, left + 0x1C, top, 0x50, 0x1C, 0x50, extra_height, alpha);
        KeyWindowColumn(board, left + 0x1C + 0x80, top, 0xD2 - 0x80, 0x1C + 0x80, 0xD2 - 0x80, extra_height, alpha);
    } else {
        KeyWindowColumn(board, left + 0x1C, top, 0xD2, 0x1C, 0xD2, extra_height, alpha);
    }
    KeyWindowColumn(board, left + 0x1C - edge, top, edge, 0x14, 6, extra_height, alpha);
    KeyWindowColumn(board, left + 8 - edge, top, 0x14, 0, 0x14, extra_height, alpha);
    KeyWindowColumn(board, left + 0xEE, top, edge, 0xEC, 6, extra_height, alpha);
    KeyWindowColumn(board, left + 0xEE + edge, top, 0x20, 0xF4, 0x20, extra_height, alpha);

    // The cursor's frame and the hand.
    const int cx = left + 6 + (pick.cursor % 5) * 0x2A;
    const int cy = top + 60 + (pick.cursor / 5 - first_row) * 0x28;

    if (stay != nullptr) {
        DrawMenuWaku((float) (cx + 0x10), (float) (cy - 9), 0x24, 0x24, 0, stay, alpha);
        DrawMenuObjectVibe(cx, cy, 1, 0x40);
    }

    const PickEntry &current = pick.items[std::max(0, std::min(pick.cursor, pick.count - 1))];

    if (Mutated("keywintext")) { // coop-usebands2's twin: our caption lines in the box, as before
        dc::text::caption::LineStyle style;

        style.status_font = true;
        style.edge_width = 1.5f;

        const float centre = left + 0x1C + 0xD2 / 2.0f;

        dc::text::caption::DrawLine("Use which item?", centre, (float) (cells_bottom + 14), 8.0f, 1.0f, 200.0f, style, 1.0f, 1.0f, nullptr);
        dc::text::caption::DrawLine(PickItemName(current.item) + (current.shared ? "  (shared)" : ""), centre, (float) (cells_bottom + 28), 8.0f, 1.0f,
                          200.0f, style, 1.0f, 1.0f, nullptr);
    } else {
        // coop-usebands2 (Jep, 2026-11-01: "return the item description box to the way the decomp shows it"): the item under the
        // cursor's own message (500 + its number, in eventmnu2.pak's eventuse.bin) in the box, as EventItemSelectDraw draws it.
        // Draw state on the picker owner's copy: a window of its own, set up as InitMenuMesSet(MENU_MES_SET_EVENT_ITEM) sets the
        // game's CommonMenuMes2 for its window (the game's own windows are left as they are); laid out when the item changes.
        struct Box {
            ClsMes                    mes;
            int                       made = -1;
            alignas(64) unsigned char tex[0x100] = {};
        };
        static Box  boxes[2];
        Box        &box = boxes[player - 1];
        const int   message = current.item > 0 ? current.item + 500 : 0;
        short      *messages = KeyWindowMessages();
        DrawRng     rng; // coop-drawrng: the draw stream

        if (messages != nullptr && box.made != message) {
            ClsMes &mes = box.mes;

            mes = ClsMes();
            mes.tex_buff = box.tex;
            // (ResetMenuMes)
            mes.text_columns = MES_WIN_COLUMNS;
            mes.text_rows = 10;
            mes.fade = 0.0f;
            mes.fade_in = true;
            mes.text_rate = mes.text_rate_set;
            mes.InitMesWinTbl();
            mes.clut_now = mes.clut_default;
            mes.edge_alpha = 0x80;
            for (int i = 0; i < 10; i++) {
                mes.mes_no[i] = -1;
                mes.line_pos[i].x = -1;
                mes.line_pos[i].y = -1;
            }
            mes.value_show = true;
            mes.space_width = -1;
            mes.space_area = -1;
            mes.cursor_row = -1;
            // (InitMenuMesSet)
            mes.tex_block = 0x1A;
            mes.SetBuff(messages);
            mes.SetBuff_system(SystemMes);
            if (lang == LANG_JAPANESE) {
                mes.columns = 15;
                mes.rows = 3;
                mes.char_width = 0x10;
                mes.char_height = 0x16;
            } else {
                mes.rows = 4;
                mes.char_width = 11;
                mes.char_height = 0x14;
                mes.narrow_gaiji_set = 2;
            }
            mes.stay_frame = false;
            mes.auto_pos = MES_POS_NONE;
            mes.style = MES_EDGE_NONE;
            // (its MENU_MES_SET_EVENT_ITEM case)
            mes.Preset(MES_PRESET_SYSTEM);
            mes.centre_rows = true;
            mes.columns = 0x19;
            mes.rows = 4;
            mes.mes_made = -1;
            mes.MakeMesWin(message);
            box.made = message;
        }

#ifdef PAL
        static const signed char kMessageX[7] = {0, -4, -4, -9, -4, -7, -6};
#else
        static const signed char kMessageX[7] = {0, -4, -4, -4, -4, -4, -4};
#endif
        if (box.made >= 0) {
            box.mes.edge_alpha = alpha;
            DrawMenuClsMes(&box.mes, 20 + left + kMessageX[std::clamp(lang, 0, 6)], 146 + top);
        }
    }
    setbilinear(1);

    // With DC_COOP_DEBUG, once a second while open: where it is (640x480), for the test's frame check.
    static std::array<int, 2> ticks{};
    // (coop-warmreset, WS7: put back at each warm-pair job as a fresh copy has it; trace_once_kept twin: kept)
    COOP_TEST_TRACE_RESET(ticks, (std::array<int, 2>{}));

    if (CoopDebug() && ++ticks[player - 1] % 60 == 1) {
        std::fprintf(stderr, "coop: player %d's key picker window drawn on this copy: board at (%d %d) 278x%d, %d items, cursor %d\n", player, left, top,
                     0x94 + extra_height + 0x32 + 0x1E, pick.count, pick.cursor);
    }

    return true;
}

void DrawOneKeyPick(int player) {
    const KeyPick &g_pick = g_picks[player - 1];
    const int      local = NetplayLocalPlayer();

    static bool said[2] = {};

    if (!g_pick.open) {
        said[player - 1] = false;
    }

    if ((!g_pick.open && g_pick.note_ticks == 0) || (local != 0 && local != player && !Mutated("keypickboth"))) {
        return;
    }

    // coop-doorprompt: once per opening, which player's picker this copy draws (the tests read whose screen shows it).
    if (g_pick.open && !said[player - 1]) {
        said[player - 1] = true;
        std::fprintf(stderr, "coop: player %d's key picker%s drawn on this copy (player %d)\n", player, g_pick.town ? " at the cave door" : "", local);
        std::fflush(stderr);
    }

    dc::text::caption::LineStyle style;

    style.status_font = true;
    style.edge_width = 1.5f;

    static const spRGBA kBack = {0x08, 0x10, 0x48, 0x70};
    const int           kLeft = player == 1 ? 40 : 400; // his on the left, hers on the right (couch: both)
    constexpr int       kTop = 150;
    const float         kCentre = kLeft + 100.0f;

    if (!g_pick.open) {
        set2DSpriteC4(Vif1Packet, CRect_i_(kLeft, kTop - 4, 200, 20), (spRGBA *) &kBack, (spRGBA *) &kBack, (spRGBA *) &kBack,
                      (spRGBA *) &kBack);
        dc::text::caption::DrawLine(g_pick.note, kCentre, (float) kTop, 8.0f, 1.0f, 190.0f, style, 1.0f, 1.0f, nullptr);
        return;
    }

    if (DrawKeyWindow(g_pick, player, local)) {
        return;
    }

    // Nine rows at most, scrolled with the cursor (as the overlay's pages scroll).
    int first = std::max(0, std::min(g_pick.cursor - 4, g_pick.count - 9));
    int shown = std::min(g_pick.count - first, 9);

    set2DSpriteC4(Vif1Packet, CRect_i_(kLeft, kTop - 4, 200, 30 + 11 * shown), (spRGBA *) &kBack, (spRGBA *) &kBack,
                  (spRGBA *) &kBack, (spRGBA *) &kBack);
    dc::text::caption::DrawLine("Use which item?", kCentre, (float) kTop, 8.0f, 1.0f, 190.0f, style, 1.0f, 1.0f, nullptr);

    for (int row = 0; row < shown; row++) {
        int                i = first + row;
        bool               current = i == g_pick.cursor;
        dc::text::caption::LineStyle row_style = style;
        std::string        text = PickItemName(g_pick.items[i].item) + (g_pick.items[i].shared ? "  (shared)" : "");

        if (current) {
            row_style.fill[2] = 120;
            text = "> " + text;
        }

        dc::text::caption::DrawLine(text, kCentre, (float) kTop + 12.0f + 11.0f * row, 8.0f, 1.0f, 190.0f, row_style, current ? 1.0f : 0.75f,
                          1.0f, nullptr);
    }

    dc::text::caption::DrawLine("X use   O close", kCentre, (float) kTop + 14.0f + 11.0f * shown, 7.0f, 1.0f, 190.0f, style, 0.8f,
                      1.0f, nullptr);
}

void DrawKeyPick() {
    DrawOneKeyPick(1);
    DrawOneKeyPick(2);
}

} // namespace coop

// coop-dungeonp2
namespace dc::game { // repin: was PORT_OVERRIDE; a recipe removes the game's own definition
void CRandomItem::Draw() {
    // On the partner's own copy the floor is drawn round her (CoopViewFrame), so her items draw within
    // 200 of her rather than of the player (Jep: P2 can't see drops). Draw only: CheckPosition's
    // distance[], which the tick reads, is untouched. DC_COOP_MUTATE=dropview is the twin.
    bool          own_view = CoopViewFrame() != CharaFrame && !coop::Mutated("dropview");
    sceVu0FVECTOR viewer;
    sceVu0CopyVector(viewer, coop::g_partner.pos);
    viewer[3] = 1.0f;

    for (int i = 0; i < 32; i++) {
        if (id[i] == -1 || (own_view ? DistVector(position[i], viewer) : distance[i]) > 200.0f) {
            continue;
        }

        sceVu0FVECTOR draw_position;
        int           texel_x = 0; // 9418a550 starts both at 0
        int           texel_y = 0;
        sceVu0CopyVector(draw_position, position[i]);
        draw_position[1] += 8.0f * sinf(phase[i]);

        if (amount[i] != -1) {
            texel_x = 0x40;
            texel_y = 0;
        } else {
            switch (item_no[i]) {
                case ITEM_DRAN_S_CREST:
                case ITEM_HOOK:
                case ITEM_KING_S_SLATE:
                case ITEM_GUN_POWDER:
                case ITEM_CLOCK_HANDS:
                case ITEM_POINTY_CHESTNUT:
                case ITEM_BLACK_KNIGHT_CREST:
                    texel_x = 0;
                    texel_y = 0;
                    break;
                case ITEM_SHINY_STONE:
                    texel_x = 0x40;
                    texel_y = 0x40;
                    break;
                case ITEM_RED_BERRY:
                    texel_x = 0;
                    texel_y = 0x40;
                    break;
                default:
                    texel_x = 0x40;
                    texel_y = 0xC0;
                    break;
            }

            if (item_no[i] >= ITEM_ATTACH_START && item_no[i] < ITEM_ATTACH_ELEMENT_END) {
                texel_x = 0;
                texel_y = 0x80;
            }

            if (item_no[i] >= ITEM_ATTACH_SLAYER_START && item_no[i] < ITEM_ATTACH_SLAYER_END) {
                texel_x = 0x40;
                texel_y = 0x80;
            }
        }

        BtSet3DCellModel(draw_position, gold_texture, 3.5f, texel_x, texel_y, 0x40, 0x40, 0x80);
        CamTestDropDrawn(position[i]);
    }
}
} // namespace dc::game

namespace coop {

// coop-p2marks: DC_COOP_P2MARK_OFF=T (the mark test's frame check): her mark is not drawn at press-clock ticks T to T+2, both
// copies alike, so one frame of each screen shows the same moment without it.
bool PartnerMarkHeld() {
    static const int off = TestEnvInt("DC_COOP_P2MARK_OFF", -1);
    const std::int64_t clock = off >= 0 ? NetplayTestPressClock() : -1;

    return Mutated("nomarks") || (off >= 0 && clock >= off && clock <= off + 2);
}

// Draws the game's "!" at at (a point over her head, which bobs as his does) and notes where on screen it went (640x480).
// With DC_COOP_DEBUG, once a second: how many of the last 60 frames drew it and where, for the test's frame check.
namespace {
int g_mark_frames = 0; // (DrawPartnerMarkAt's log window: frames and the ones that drew it)
int g_mark_shown = 0;
} // namespace

void PartnerMarkLogReset() {
    g_mark_frames = 0;
    g_mark_shown = 0;
}

void DrawPartnerMarkAt(CFrame *mark, const float *at, bool drawn) {
    int        &frames = g_mark_frames;
    int        &shown = g_mark_shown;
    static int screen_at[2] = {-1, -1};

    if (drawn && mark != nullptr) {
        float point[4] = {at[0], at[1], at[2], 1.0f};
        int   screen[4];

        mark->SetPosition(point);
        mark->attr.billboard = 2;
        MGDraw(mark);
        shown++;
        screen_at[0] = screen_at[1] = -1;
        if (MGRotTransPers(screen, point, 0) != 0) {
            screen_at[0] = (screen[0] - 0x6C00) >> 4;
            screen_at[1] = ((screen[1] - GS_Y_OFFSET) >> 4) * 2;
        }
    }

    if (!CoopDebug() || ++frames < 60) {
        return;
    }

    std::fprintf(stderr, "coop-p2marks: her mark drawn %d/60 frames, copy of p%d, at (%d %d) clock %lld\n", shown, NetplayLocalPlayer() == 2 ? 2 : 1,
                 shown ? screen_at[0] : -1, shown ? screen_at[1] : -1, (long long) NetplayTestPressClock());
    frames = shown = 0;
}

// What her Cross or Square would act on where she stands, as DunMoveChara marks his: an event slot whose script her Cross
// starts (Interact) or whose lock her Square answers (InteractItem), or a chest, mimic or Atla in reach. Not while she opens
// a chest, lies down, or has her own prompt or picker open.
static bool PartnerMarkInReach() {
    if (!g_partner.visible || g_partner.chara == nullptr || g_partner.down != 0 || g_partner.busy != 0 || gameTask != GAME_TASK_PLAY ||
        BtActStatus.can_act == 0 || BtActStatus.movement_locked != 0 || NowEventMan == nullptr || NowDngMap == nullptr || UsePromptOpen(2) ||
        KeyPickOpenFor(2)) {
        return false;
    }

    CDungeonEventData *state = NowEventMan->SearchDataSlotPos2(g_partner.pos);

    if (state != nullptr && state->event->script_no != -1 && state->event->fade == 0 &&
        !(state->event->chara_no != -1 && state->chara_done == g_partner.loaded_chara)) {
        return true;
    }

    const int active = NowDngMap->GetActiveIvent((CFrameVu1 *) g_partner.chara->frame);

    if (active == -1) {
        return false;
    }

    switch (NowDngMap->events[active].kind) {
        case DNG_EVENT_TREASURE_BOX:
        case DNG_EVENT_MIMIC:
            return true;
        case DNG_EVENT_ATRA:
            return NowDngMap->atra[NowDngMap->events[active].index].used != 0;
        default:
            return false;
    }
}

} // namespace coop

// coop-p2marks (Jep's 2026-10-27 playtest: "P2 gets no exclamation mark at the locked door or at chests, though Square and X
// still work"): retail's mark (BtEventInfo.event_marker) is set from his spot alone and drawn over him. Hers is drawn over her,
// at his height above the feet (22 + the member's height - 15), on both copies as his is. DC_COOP_MUTATE=nomarks (twin): none.
void CoopDrawPartnerMark(CFrame *mark) {
    using namespace coop;
    static const float kMemberHeight[6] = {16.0f, 14.0f, 16.0f, 16.0f, 18.0f, 15.0f}; // gameloop's CharaHeight
    static float       bob = -PI_SHORT;
    COOP_TEST_JOB_RESET(bob, -PI_SHORT, "job_scratch_kept"); // (coop-warmreset, WS7: the mark's bob from its start each job)

    const bool drawn = !PartnerMarkHeld() && PartnerMarkInReach();
    float      at[3] = {g_partner.pos[0], g_partner.pos[1], g_partner.pos[2]};

    if (drawn) {
        const int member = g_partner.loaded_chara >= 0 && g_partner.loaded_chara < 6 ? g_partner.loaded_chara : 0;

        bob += 0.104719736f;
        if (bob >= PI_SHORT) {
            bob -= TWO_PI_SHORT;
        }

        at[1] += 7.0f + kMemberHeight[member] + 0.5f * sinf(bob);
    }

    DrawPartnerMarkAt(mark, at, drawn);
}
