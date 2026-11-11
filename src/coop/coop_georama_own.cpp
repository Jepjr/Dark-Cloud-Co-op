// coop-town: player 2's own Georama in a linked town (Jep, 2026-10-20: "either of them ... using georama and it not
// restrict the other player"). Player 2's Select, while she walks the town, opens a Georama of her own: the editor's
// cursor, its camera, moving and turning parts, steered by her pad. Player 1's town goes on as it is (the town's mode
// stays his: walking, talking, shops, doors, events). Each tick both linked copies step her editor alike, from the
// pad they agreed on, with the town's mode, its camera and the game pad's first port swapped in for the step and
// back after (as coop_fullmenu steps player 2's menu), so the lockstep holds; only her copy draws it.
// One Georama at a time for now: hers does not open while player 1 builds, and his Select waits while she builds.
// Not yet: the parts menu (Triangle) and the events placing parts starts (the next steps). It closes on her Select
// (she stands where the cursor stood), or when the town's map changes or the copies go apart (she stays where she
// was). DC_COOP_MUTATE=p2georama_off: her Select opens nothing (twin).

#include "coop/game_ns.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>

#include "camerafollow.hpp"
#include "coop/coop.hpp"
#include "coop/coop_actor.hpp" // coop-stage4 4d: PlayerInMenu
#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>
#include "coop/coop_audio.hpp" // coop-menuse
#include "coop/coop_core.hpp"
#include "coop/coop_georama.hpp"
#include "coop/coop_writeguard.hpp"
#include "coop/coop_camera.hpp"   // coop-stage4: DrawRng (her local parts menu)
#include "coop/coop_fullmenu.hpp" // coop-stage4: FullMenuLocalOn
#include "coop/netplay_cmd.hpp"   // coop-stage4: kCmdGeoMenu
#include "coop/coop_town.hpp"
#include "coop/coop_townmenu.hpp"
#include "coop/coop_worlds.hpp"
#include "coop/netplay.hpp"
#include "edit.hpp"
#include "editarea.hpp"
#include "editground.hpp"
#include "editloop.hpp"
#include "editmapscript.hpp"
#include "editmenu.hpp"
#include "editloop3.hpp"
#include "editpartsinfo.hpp"
#include "npcharacter.hpp"
#include "savedata.hpp"
#include "clsmes.hpp"
#include "gamepad.hpp"
#include "gfx/gfx.hpp"
#include "texture.hpp"
#include "mglib.hpp"
#include "rect.hpp"
#include "snd.hpp"
#include "mathutil.hpp"
#include "sysmes.hpp"
#include "platform/input.hpp"

using namespace coop;

namespace dc::game {
extern s32 mode;
extern s32 MapNo;
void       PlayTimeCountFlag(int on); // (coop-stage4 4d)
int        CheckEditToWalk(float *position);
void       EditMode();
int        FadeOutToEvent(int event_no, int level);
void       EdSaveFrameImage(CTexture texture);
int        EditMenuLoop();
void       EditMenuInit(int *texture_blocks, int start_at_atora);
} // namespace dc::game

namespace {

constexpr int kTownMode = 2; // GAME_MODE_EDIT

// The build mode's editor as the game keeps it in globals (its cursor, camera, focus and selection, the menu's
// answer): player 1's in the globals themselves, player 2's in her own copy, swapped in around her step and her
// copy's frame, so both can build at once (Jep, 2026-10-21: "both able to use the georama menu at the same time").
struct Editor {
    // The edit camera's state from its eye on (CCamera::pos to the end; its frame stays EditCamera's own). Not its
    // operator=, which copies only what it follows, its distance and height: with that the two editors shared one eye,
    // its angle and its steps, and each camera sat between both cursors (Jep's playtest, 2026-10-21).
    unsigned char camera[sizeof(CCameraFollow)] = {};
    sceVu0FVECTOR    frame_pos = {0.0f, 0.0f, 0.0f, 1.0f};
    sceVu0FVECTOR    frame_rot = {0.0f, 0.0f, 0.0f, 0.0f};
    sceVu0FVECTOR    now_cursor = {0.0f, 0.0f, 0.0f, 1.0f};
    sceVu0FVECTOR    next_cursor = {0.0f, 0.0f, 0.0f, 1.0f};
    float            parts_cursor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    CMapParts       *focus = nullptr;
    CMapParts       *old_focus = nullptr;
    int              parts_name = -1;
    int              draw_name_count = 30;
    int              select_angle = 0;
    int              old_select_angle = 0;
    float            rot_y = 0.0f;
    int              select_parts = -1;
    EDIT_MENU_STATUS menu_status{};
    int              move_count = 0;
};

constexpr std::size_t kCameraFrom = offsetof(CCameraFollow, pos);

// DC_COOP_MUTATE=p2georama_camera_operator: the game's own operator= (what it follows, distance, height), as before (twin).
bool CameraOperator() {
    static const bool old = Mutated("p2georama_camera_operator");
    return old;
}

CCameraFollow g_camera_old{60.0f, 20.0f, 0.0f, 4.0f};

void CameraSave(unsigned char *to) {
    if (CameraOperator()) {
        g_camera_old = EditCamera;
        return;
    }

    std::memcpy(to + kCameraFrom, reinterpret_cast<const unsigned char *>(&EditCamera) + kCameraFrom, sizeof(CCameraFollow) - kCameraFrom);
}

void CameraLoad(const unsigned char *from) {
    if (CameraOperator()) {
        EditCamera = g_camera_old;
        return;
    }

    std::memcpy(reinterpret_cast<unsigned char *>(&EditCamera) + kCameraFrom, from + kCameraFrom, sizeof(CCameraFollow) - kCameraFrom);
}

void SaveEditor(Editor &e) {
    CameraSave(e.camera);
    sceVu0CopyVector(e.frame_pos, ECursorFrame->position);
    ECursorFrame->GetRotation(e.frame_rot);
    sceVu0CopyVector(e.now_cursor, NowCursorPos);
    sceVu0CopyVector(e.next_cursor, NextCursorPos);
    sceVu0CopyVector(e.parts_cursor, NowPartsCursorPos);
    e.focus = NowFocusParts;
    e.old_focus = OldFocusParts;
    e.parts_name = PartsNameNum;
    e.draw_name_count = DrawPartsNameCount;
    e.select_angle = NowSelectAngle;
    e.old_select_angle = OldSelectAngle;
    e.rot_y = NowCursorRotY;
    e.select_parts = NowSelectParts;
    e.menu_status = EditMenuStatus;
    e.move_count = move_count;
}

void LoadEditor(const Editor &e) {
    CameraLoad(e.camera);
    ECursorFrame->SetPosition((float *) e.frame_pos);
    ECursorFrame->SetRotation(e.frame_rot[0], e.frame_rot[1], e.frame_rot[2]);
    sceVu0CopyVector(NowCursorPos, (float *) e.now_cursor);
    sceVu0CopyVector(NextCursorPos, (float *) e.next_cursor);
    sceVu0CopyVector(NowPartsCursorPos, (float *) e.parts_cursor);
    NowFocusParts = e.focus;
    OldFocusParts = e.old_focus;
    PartsNameNum = e.parts_name;
    DrawPartsNameCount = e.draw_name_count;
    NowSelectAngle = e.select_angle;
    OldSelectAngle = e.old_select_angle;
    NowCursorRotY = e.rot_y;
    NowSelectParts = e.select_parts;
    EditMenuStatus = e.menu_status;
    move_count = e.move_count;
}

struct Own {
    bool          on = false;
    Editor        editor;           // her editor while it is not swapped in
    bool          swapped = false;  // her editor is in the globals now (player 1's in town_editor)
    int           map = -1;
    int           ticks = 0;
    std::uint16_t prev = 0;
    InputPadState before{};
    int           town_mode = -1; // the town's own mode, for the log
    // Her own game pad (its auto-repeat and menu mode go on from tick to tick), her pad on its first port.
    CGamePad pad;
    // Her parts menu (B2): 0 shut, 1 opening (the frames retail waits before it keeps the still), 2 open.
    int menu = 0;
    int menu_ticks = 0;
    bool still_wanted = false; // her copy keeps the menu's still after this frame is drawn (as EdInitModeFinish, after the draw)
    bool still_kept = false;
    bool paused = false;       // her own pause at her Georama (Start): her editor holds, the world goes on
    int menu_opens = 0;
    bool menu_sent = false; // coop-stage4: her local parts menu shut on her copy, its close command sent (every copy shuts it on its tick)
    // An event her building starts (B3: the menu's event, the town's completion), held until player 1 walks.
    int event = 0;
    int event_level = 0;
    // coop-stage4 r4: carried into a merge by her state (CoopGeoramaHerToOwn): held as it is until both copies are together.
    bool merge_hold = false;
};

gfx::TextureHandle g_hidden = gfx::kNullTexture;

Own    g_own;
Editor g_town_editor; // player 1's editor while hers is swapped in

// DC_COOP_MUTATE=p2georama_shared_editor: her editor is the globals themselves, player 1's too (twin).
bool SharedEditor() {
    static const bool shared = Mutated("p2georama_shared_editor");
    return shared;
}

void SwapIn() {
    if (g_own.swapped || SharedEditor()) {
        return;
    }

    SaveEditor(g_town_editor);
    LoadEditor(g_own.editor);
    g_own.swapped = true;
}

void SwapOut() {
    if (!g_own.swapped) {
        return;
    }

    SaveEditor(g_own.editor);
    LoadEditor(g_town_editor);
    g_own.swapped = false;
}

bool Allowed() {
    return coop::Enabled() && NetplayLocalPlayer() != 0 && !coop::Mutated("p2georama_off") && mode == kTownMode && EdInteriorFlag == 0 &&
           !CoopWorldsApart() && coop::TownPartnerShown(nullptr, nullptr);
}

// A menu of the town's own open or opening (his Triangle menu, a shop, an item, the Georama's): the town's menus
// share one engine, its textures and its buffer, so hers waits, and his waits while hers is open.
// mode: the town's own mode (inside her step the globals hold hers, so the step passes the town's it kept).
// coop-stage4 (Jep, 2026-11-07: menus local for every player): her parts menu runs on her copy alone, as her town menu does
// (coop_townmenu 4a). Her Triangle (her agreed pad) opens it on every copy (g_own.menu, so her editor holds alike), but only her
// copy makes it (EditMenuInit: emenu.pak, its textures, the pad's menu mode on her pad) and steps it, its random numbers her
// copy's own (DrawRng). What it hands back (EditMenuStatus: the mode, the part picked, the Atla event) goes out as one command
// (kCmdGeoMenu) that shuts it on every copy on its tick. The other copy no longer steps or draws it out of sight (the hidden
// target, a bandaid of the lockstep path) and its town menu engine is left alone. DC_COOP_MUTATE=p2georama_menu_lockstep (twin):
// stepped on every copy, as before.
bool PartsMenuLocal() {
    return FullMenuLocalOn() && !coop::Mutated("p2georama_menu_lockstep");
}

// This copy is the one her menu runs on (the owner's: player 2, the Georama's own player).
constexpr int kOwnPlayer = 2;
bool          PartsMenuHere() {
    return NetplayLocalPlayer() == kOwnPlayer;
}

// coop-p1parts (Jep 11-07: menus local for every player): his parts menu as hers above. Linked, the town's mode goes through retail's
// GEORAMA_MENU_INIT and GEORAMA_MENU on every copy (his editor holds alike, she walks on under it), but only his copy makes the menu
// (EdInitModeFinish, its still, its textures, the pad's menu mode) and steps it, its random numbers his copy's own (DrawRng) and what it
// writes (its Save page's status) as pokes. What it hands back goes out as kCmdGeoMenu, for player 1, and every copy leaves the mode by
// retail's own close on its tick. Apart it is retail's on the copy's own player; from the taken state set on (Linked) it is local, so an
// open one goes on through a merge, the taker never stepping it. DC_COOP_MUTATE=p1parts_menu_lockstep (twin): stepped on every copy, as before.
constexpr int kHisPlayer = 1;
// coop-soakfix4 (soak seed 1078: the joiner's 0xC0000005 in options::FirstRow): and from the link's start, through a join's catch-up
// too (Jep 11-10: his menus are his copy's; a join never affects him or waits). His parts menu open on its Option page as she
// joined: the state set carried the menu's globals, her replay stepped his menu (the link not yet its players', LocalMenusOn
// false), and the port's Options screen it ran there was never opened on her copy (its state, options::g_screen, is his copy's
// alone, outside the set). Linked, his copy steps it alone and its close is the command (kCmdGeoMenu), recorded in the
// catch-up as any command. DC_COOP_MUTATE=p1parts_catchup_shared (twin): not before the lockstep, as before.
static bool HisPartsLocalInCatchup() {
    return NetplayLinkUp() && FullMenuLocalOnLinkUp() && !coop::Mutated("p2georama_menu_lockstep") && !coop::Mutated("p1parts_catchup_shared");
}
bool          HisPartsLocal() {
    return (PartsMenuLocal() || HisPartsLocalInCatchup()) && !coop::Mutated("p1parts_menu_lockstep") && CoopTownMenuLinked();
}
bool g_his_parts_sent = false; // his copy: shut here, its close command sent
bool g_his_parts_shut = false; // every copy: its close command came, retail's close runs on this step
int  g_his_parts_ticks = 0;

// His parts menu run in the lockstep (its fix-off twin p1parts_menu_lockstep) shares her copy's one engine with hers (its buffer and texture
// blocks), so hers waits; local, its engine is never on her copy (DC_COOP_MUTATE=p1parts_one_at_a_time, twin: it waits, as before).
// (coop-scriptmenus: TownMenuBusy is gone. Its last job was the menus a talk's script opens, retail's mode on both copies on her copy's
// engine; they now run on their opener's copy alone (coop_scriptmenu.cpp), and the opener is in that event, not in her Georama. Retail's
// walk menus (ED_MODE_MENU) are all local too: the battle menu, the overflow and the shops goto_menu opens, its only menus.)
bool HisPartsMenuOnHerEngine(int mode) {
    return (mode == ED_MODE_GEORAMA_MENU_INIT || mode == ED_MODE_GEORAMA_MENU) && (!HisPartsLocal() || coop::Mutated("p1parts_one_at_a_time"));
}

bool TownBuilds() {
    return GameMode == ED_MODE_GEORAMA || GameMode == ED_MODE_GEORAMA_MENU_INIT || GameMode == ED_MODE_GEORAMA_MENU ||
           GameMode == ED_MODE_RETURN_MENU_GEORAMA;
}

// Her pad on the game pad's first port, as it is now and as it was the tick before (Down reads the change).
void SetPortOne(CGamePad &pad, const InputPadState &now, const InputPadState &before) {
    PAD_DATA *data = reinterpret_cast<PAD_DATA *>(&pad); // pad[0], pad[1], previous_pad[0], previous_pad[1]

    for (int i : {0, 2}) {
        const InputPadState &from = i == 0 ? now : before;
        PAD_STATUS          &to = data[i].input.status;

        to.button = from.buttons & 0xFFFF;
        to.left_x = from.left_x;
        to.left_y = from.left_y;
        to.right_x = from.right_x;
        to.right_y = from.right_y;
    }
}

// Her cursor: in the globals while her editor is swapped in (or shared), else in her copy.
const float *Cursor() {
    static const float none[4] = {0.0f, 0.0f, 0.0f, 1.0f};

    if (g_own.swapped || SharedEditor()) {
        return ECursorFrame != nullptr ? ECursorFrame->position : none;
    }

    return g_own.editor.frame_pos;
}

// Parts standing on the map, all kinds together.
int PlacedParts() {
    int placed = 0;

    for (int i = 0; i < EditPartsInfo.parts_max && i < 24; i++) {
        if (EDITPARTS_INFO *info = EditPartsInfo.GetPartsInfo(i)) {
            placed += info->placed;
        }
    }

    return placed;
}

void Log(const char *what) {
    sceVu0FVECTOR toan = {0.0f, 0.0f, 0.0f, 1.0f};

    if (Chara != nullptr) {
        Chara->GetPosition(toan);
    }

    std::printf("coop-georama: player 2's own Georama %s at tick %d, cursor (%.0f %.0f), Toan (%.0f %.0f) mode %d, placed %d\n", what, g_own.ticks,
                Cursor()[0], Cursor()[2], toan[0], toan[2],
                GameMode == ED_MODE_GEORAMA && g_own.town_mode >= 0 ? g_own.town_mode : GameMode, PlacedParts());
}

// The ground a map's save holds, as editloop.cpp's ED_GRD_DATA: only whether its build event was seen.
struct GroundSave {
    u8  unk_00[0x64];
    int complete_event_seen;
};

// Holds an event her Georama starts, as FadeOutToEvent would take it (a higher level replaces a lower one).
void Queue(int event_no, int level, const char *why) {
    if (event_no <= 0 || (g_own.event != 0 && level <= g_own.event_level)) {
        return;
    }

    g_own.event = event_no;
    g_own.event_level = level;
    std::printf("coop-georama: player 2's Georama event %d (%s) waits until player 1 walks\n", event_no, why);
}

// Her way out with Select: the events retail's way out starts (the town complete, Matataki's request 16), and the
// test's (DC_COOP_P2GEORAMA_TEST_EVENT=N, its level 9).
void QueueLeaveEvents() {
    static const int test_event = TestEnvInt("DC_COOP_P2GEORAMA_TEST_EVENT", 0);
    const GroundSave *ground = (const GroundSave *) SaveData->GetGrdData(MapNo);

    if (ground != NULL && ground->complete_event_seen == 0) {
        int requested = 0;

        for (int i = 0; i < 24; i++) {
            requested += EditPartsInfo.request[i] != 0;
        }

        if (requested == EditPartsInfo.parts_max) {
            Queue(0x83, 9, "the town complete");
        }
    }

    if (MapNo == TOWN_MATATAKI && SaveData->GetGameFlag(0x14) == 0 && EditPartsInfo.request[16] != 0) {
        Queue(0xB, 0xA, "Matataki's request");
    }

    if (test_event > 0) {
        Queue(test_event, 9, "test");

        // DC_COOP_P2GEORAMA_TEST_BUSY=1: the town clock to just before dusk as it is queued, so player 1's town plays
        // the time of day's event first (both copies alike) and hers must wait it out.
        if (TestEnvInt("DC_COOP_P2GEORAMA_TEST_BUSY", 0) != 0) {
            NowTime = 2.4999f;
            SaveData->SetNowTime(2.4999f);
            change_time_event = 1; // the clock's own way to the event (0x84), whatever hour the clock was at
            std::printf("coop-georama: test: the clock to 2.4999 as her event waits\n");
        }
    }
}

// ---- a part on the other player (Jep, 2026-10-21) -----------------------------------------------------------------
// Retail checks nothing about characters when a part goes down (SetMapParts asks only whether the plot is free): Toan
// is not in the world while he builds and comes back at the cursor onto walkable ground, and the villagers go back to
// their places as build mode ends. In co-op the other player stands in the town meanwhile: when a part goes down on
// them (Jep chose, 2026-10-21: push the player out), their character goes to the nearest spot out of every part on
// the build area, by a search the same on both copies. DC_COOP_MUTATE=georama_nopush: left inside (twin).
bool InsidePart(const float *p) {
    CMapParts *part = pEditGround->GetParts(p[0], p[1], p[2]);

    return part != nullptr && part->subtype != MAP_PARTS_SUBTYPE_ROAD;
}

// The nearest spot out of every part and on the build area: rings of cells around p, each in a fixed order.
bool FreeSpot(const float *p, float *out) {
    float cell = pEditGround->areas[0] != nullptr ? pEditGround->areas[0]->unit_size : 10.0f;

    for (int ring = 1; ring <= 16; ring++) {
        for (int dz = -ring; dz <= ring; dz++) {
            for (int dx = -ring; dx <= ring; dx++) {
                if (dx != -ring && dx != ring && dz != -ring && dz != ring) {
                    continue;
                }

                float at[4] = {p[0] + dx * cell, p[1], p[2] + dz * cell, 1.0f};

                if (!InsidePart(at) && pEditGround->GetAreaCode(at[0], at[1], at[2]) >= 0) {
                    out[0] = at[0];
                    out[1] = at[1];
                    out[2] = at[2];
                    out[3] = 1.0f;
                    return true;
                }
            }
        }
    }

    return false;
}

// Player 1 (Toan, walking) or player 2 (her character) out of a part just put down.
void PushOut(int player) {
    float at[4] = {0.0f, 0.0f, 0.0f, 1.0f};

    if (player == 1) {
        if (Chara == nullptr) {
            return;
        }

        Chara->GetPosition(at);
    } else if (!coop::TownPartnerShown(at, nullptr)) {
        return;
    }

    bool  inside = InsidePart(at);
    float to[4] = {0.0f, 0.0f, 0.0f, 1.0f};

    if (!inside || Mutated("georama_nopush") || !FreeSpot(at, to)) {
        std::printf("coop-georama: a part went down; player %d %s it at (%.0f %.0f)\n", player, inside ? "left inside" : "clear of", at[0], at[2]);
        return;
    }

    if (player == 1) {
        Chara->SetPosition(to);
        Chara->frame->SetPosition(to);
        Chara->Step();
    } else {
        coop::TownPlacePartner(to);
    }

    std::printf("coop-georama: a part went down on player %d at (%.0f %.0f); put at (%.0f %.0f), %s\n", player, at[0], at[2], to[0], to[2],
                InsidePart(to) ? "still inside" : "outside every part");
}

// ---- a part held by the other builder (Jep's playtest, 2026-10-30: "if p1 starts to put down a house in georama, p2 can
// pick the same house and it's still available for them to pick so they can duplicate it") ----------------------------
// A part in hand (picked from the menu, or picked up off the map, which takes it off the map's count) is counted placed
// through the other builder's step: their menu shows it gone and their cursor cannot put it down (placed < stock), so
// one copy stays one. Both copies step both editors alike, so both count alike. DC_COOP_MUTATE=georama_no_reserve:
// nothing counted, as before (twin).
int g_reserved = -1; // the part counted for the other builder's step now, -1 for none

void Reserve(int part) {
    EDITPARTS_INFO *info = part >= 0 && part < 24 ? EditPartsInfo.GetPartsInfo(part) : nullptr;

    if (g_reserved >= 0 || info == nullptr || Mutated("georama_no_reserve")) {
        return;
    }

    info->placed++;
    g_reserved = part;
}

void Unreserve() {
    if (g_reserved < 0) {
        return;
    }

    if (EDITPARTS_INFO *info = EditPartsInfo.GetPartsInfo(g_reserved)) {
        info->placed--;
    }

    g_reserved = -1;
}

int g_p1_placed = -1; // the map's placed parts before player 1's step in his Georama
// What stands under the other player before a step (a river or lake can go down without the parts' count moving:
// Jep fell through the map, 2026-10-21). DC_COOP_MUTATE=georama_countonly: the count alone, as before (twin).
const CMapParts *g_under_before = nullptr;

const CMapParts *UnderPlayer(int player) {
    float at[4] = {0.0f, 0.0f, 0.0f, 1.0f};

    if (pEditGround == nullptr || (player == 1 ? Chara == nullptr : !coop::TownPartnerShown(at, nullptr))) {
        return nullptr;
    }
    if (player == 1) {
        Chara->GetPosition(at);
    }

    return pEditGround->GetParts(at[0], at[1], at[2]);
}

// A part went down where the player stands this step: what is under them changed. Not merely a part going down anywhere:
// one placed across the town put a player standing on a building (inside that part) out of it (Jep's playtest,
// 2026-10-30). DC_COOP_MUTATE=georama_pushany: the count rising is enough, as before (twin).
bool PutOn(int player, int placed_before, const CMapParts *under_before) {
    if (PlacedParts() > placed_before && (Mutated("georama_pushany") || Mutated("georama_countonly"))) {
        return true;
    }

    const CMapParts *now = UnderPlayer(player);

    return !Mutated("georama_countonly") && now != under_before && now != nullptr;
}

// The DC_COOP_TEST_P1_PLACE hook's count, reset with the session (a join's resync) so both copies pick on one tick.
int  p1_ticks = 0;
bool p1_done = false;

void Begin() {
    sceVu0FVECTOR at = {0.0f, 0.0f, 0.0f, 1.0f};

    coop::TownPartnerShown(at, nullptr);
    // Her editor starts from the globals as they are (player 1's, building or not) and is made hers below.
    SaveEditor(g_own.editor);
    SwapIn();
    move_count = 30;
    coop::TownCamPose(EditCamera);
    ECursorFrame->SetPosition(at);
    sceVu0CopyVector(NowPartsCursorPos, ECursorFrame->position);
    sceVu0CopyVector(NowCursorPos, ECursorFrame->position);
    sceVu0CopyVector(NextCursorPos, ECursorFrame->position);
    NowSelectAngle = 0;
    OldSelectAngle = 0;
    NowCursorRotY = 0.0f;
    NowFocusParts = NULL;
    OldFocusParts = NULL;
    PartsNameNum = -1;
    DrawPartsNameCount = 30;
    EditMenuStatus.mode = EDIT_MENU_MODE_CLOSED;
    EditMenuStatus.parts = -1;
    NowSelectParts = -1;
    SwapOut();
    g_own.pad = GamePad;
    g_own.pad.AutoRepeatOff();
    g_own.pad.MenuModeOff();
    g_own.menu = 0;
    g_own.on = true;
    g_own.map = MapNo;
    g_own.ticks = 0;
    Log("opens");
}

// coop-georamabar (Jep, 2026-10-27: as she closed her Georama, player 1 saw its help bar flash at the bottom of his
// screen): her editor's step sets the game's help bar (EdEditMainHelpMes, two frames' count) every tick, and the bar
// lives one more frame after her last step, until the town's frame counts it out. Player 1's copy hid it only while
// she still built, so it drew that frame. Her bar is left out of his frames for this many frames after her close.
int g_help_left = 0;

void End(const char *why) {
    if (g_own.menu == 2) {
        std::printf("coop-georama: player 2's parts menu shut with her Georama\n");
    }

    g_own.menu = 0;
    g_own.on = false;
    g_help_left = 2;
    Log(why);

    // DC_COOP_TEST_SHOT_SERIES_MARK: the frames from her close on (t-georama-bar).
    if (TestEnvOn("DC_COOP_TEST_SHOT_SERIES_MARK")) {
        NetplayTestSeriesMark();
    }
}

} // namespace

bool CoopP2Building() {
    return g_own.on;
}

bool CoopP2HelpLeftFrame() {
    if (g_own.on || g_help_left <= 0) {
        return false;
    }

    g_help_left--;
    return true;
}

bool CoopP2GeoramaHoldsP1() {
    return g_own.on && Mutated("p2georama_one_at_a_time");
}

// The editor as it stood before the frame, on both copies: what a frame's drawing does to the editor's globals (its
// camera, the part name's count and the like) is that copy's alone (her copy draws her editor, his copy his), and the
// steps read them, so after every frame they are put back as they were, the same on both (2026-10-21: one copy's
// drawing left player 1's editor otherwise than the other's, and the copies parted as he went into his Georama).
Editor g_draw_editor;
bool   g_draw_saved = false;

// The hand's glide (ParamDraw eases NowCursorPos to the cursor as it draws) is drawing state: the editor is put back
// after every frame, so the glide started over from where the step left it each frame and the hand crept behind the
// cursor and the camera (Jep, 2026-10-21). Each copy keeps its own player's hand here and the other's beside it,
// never in the editor the steps read. DC_COOP_MUTATE=georama_hand_reset: the glide put back each frame (twin).
sceVu0FVECTOR g_hand = {0.0f, 0.0f, 0.0f, 1.0f};
bool          g_hand_valid = false;
int           g_hand_frames = 0; // frames this copy's hand was drawn from its kept glide (for the test)
// coop-pt1108 (Jep's 11-08 playtest: moving a house, on the mover's own screen the red box and the house far behind the hand; right on the other's): the held part's glide
// (DrawPartsCursor eases NowPartsCursorPos to the cursor as it draws) is drawing state too, put back with the editor after every frame, so it never got anywhere. Kept as the hand's;
// a new part in hand starts from where the step put it, as the other's part does (CoopGeoramaDrawOtherCursor). DC_COOP_MUTATE=georama_part_reset (twin): put back each frame, as before.
float g_part_glide[4] = {0.0f, 0.0f, 0.0f, 1.0f};
int   g_part_held = -1;
bool  g_part_valid = false;

void CoopP2DrawEditorBegin() {
    g_draw_saved = NetplayLocalPlayer() != 0 && !CameraOperator() && ECursorFrame != nullptr && !Mutated("georama_draw_kept");

    if (g_draw_saved) {
        SaveEditor(g_draw_editor);
    }

    // Her copy draws her Georama from her editor (its camera, cursor and selection), player 1's put back after.
    if (NetplayLocalPlayer() == 2 && g_own.on && !Mutated("georama_no_draw_swap")) {
        SwapIn();

        // But the edit camera stays player 1's through the frame: the town's drawing steps and culls its effects from
        // NowCamera (DrawEffect), which player 1's copy draws from his camera, and the steps read what that leaves.
        // Her view comes from her camera all the same (CoopP2DrawCamera, for CoopTownView).
        // (2026-10-21: drawn from her camera, the copies parted whenever he went into his Georama while she built.)
        if (!Mutated("georama_draw_her_camera")) {
            CameraLoad(g_town_editor.camera);
        }
    }

    // This copy's player builds: the hand glides on from where the last frame left it.
    bool building = NetplayLocalPlayer() == 2 ? g_own.on : GameMode == ED_MODE_GEORAMA;

    if (building && g_draw_saved && g_part_valid && !Mutated("georama_part_reset") && NowSelectParts == g_part_held) {
        sceVu0CopyVector(NowPartsCursorPos, g_part_glide);
    }

    if (!building || !g_draw_saved) {
        g_hand_valid = false;
    } else if (g_hand_valid && !Mutated("georama_hand_reset")) {
        sceVu0CopyVector(NowCursorPos, g_hand);
        g_hand_frames++;
        // DC_COOP_DEBUG: every 60 frames of this copy's own hand (t-p2-georama-pause -Case his, hermenu).
        if (CoopDebug() && g_hand_frames % 60 == 1) {
            std::printf("coop-georama: p%d draws its own hand, glided %d frames\n", NetplayLocalPlayer(), g_hand_frames);
        }
    }
}

// The other builder's hand, as ParamDraw draws this copy's own (the 2D "syst08" over the cursor), gliding the same
// way on drawing state of its own (Jep, 2026-10-21: neither could see the other's hand; the part cursor drawn for
// the other has no hand when no part is held). DC_COOP_MUTATE=georama_no_other_hand: not drawn (twin).
void CoopGeoramaDrawOtherHand() {
    static sceVu0FVECTOR glide = {0.0f, 0.0f, 0.0f, 1.0f};
    static bool          gliding = false;
    static int           drawn = 0;
    const Editor        *other = nullptr;
    int                  local = NetplayLocalPlayer();

    if (local == 1 && g_own.on && !g_own.paused && g_own.menu == 0) {
        other = &g_own.editor;
    } else if (local == 2 && g_own.swapped && g_own.town_mode == ED_MODE_GEORAMA) {
        other = &g_town_editor;
    }

    // Only over this copy's own build view (GameMode is the mode the frame is drawn in): player 1 walking while she
    // built saw her hand glide over his town (Jep's playtest, 2026-10-25, note 7).
    // DC_COOP_MUTATE=georama_hand_anywhere: over any frame, as before (twin).
    // coop-pausegeo: not over this copy's pause from its Georama either (mode 10: the pause picture over a still build
    // view); the other builder goes on, but a pause shows nobody's hand. DC_COOP_MUTATE=georama_hand_in_pause: drawn
    // over the pause too, as T14 left it (twin of t-p2-georama-pause -Case bothpause).
    bool build_view = GameMode == ED_MODE_GEORAMA || GameMode == ED_MODE_GEORAMA_MENU_INIT ||
                      (GameMode == ED_MODE_RETURN_MENU_GEORAMA && Mutated("georama_hand_in_pause"));

    if (!build_view && !Mutated("georama_hand_anywhere")) {
        other = nullptr;
    }

    if (other == nullptr || Mutated("georama_no_other_hand")) {
        gliding = false;
        return;
    }

    if (!gliding) {
        // coop-pt1108 (Jep's 11-08 playtest: "when P2 closes hers, P1's hand appears to move"): the other's now_cursor is ParamDraw's glide, drawing state, so stale on this copy, which
        // never drew that editor: the hand slid over from wherever it last was each time it came back (her menu shut). It starts where its cursor is. DC_COOP_MUTATE=otherhand_slide (twin): as before.
        sceVu0CopyVector(glide, (float *) (coop::Mutated("otherhand_slide") ? other->now_cursor : other->next_cursor));
        gliding = true;
        const float *to = other->next_cursor;

        std::fprintf(stderr, "coop-georama: p%d shows the other's hand from (%.0f %.0f), its cursor at (%.0f %.0f)\n", local, glide[0], glide[2], to[0], to[2]);
    }

    VectorInterpolate(glide, glide, (float *) other->next_cursor, 4.0f, INTERPOLATE_FRACTION);
    glide[3] = 1.0f;

    sceVu0IVECTOR screen;

    TexManager.ReloadTexture(Vif1Packet, 20);
    MGRotTransPers2D(screen, glide, 0);
    set2DSprite(Vif1Packet, TexManager.GetTexture((char *) "syst08", -1), CRect_i_(screen[0] - 10, screen[1] - 32, 32, 32), 0, 0);

    if (++drawn % 60 == 1) {
        std::printf("coop-georama: p%d drew player %d's hand at (%.0f %.0f), its own hand glided %d frames, frame mode %d\n", local, 3 - local,
                    glide[0], glide[2], g_hand_frames, GameMode);
    }
}

void CoopGeoramaDrawOtherCursor(int pass) {
    if (Mutated("georama_no_other_cursor")) {
        return;
    }

    const Editor *other = nullptr;
    int           local = NetplayLocalPlayer();

    if (local == 1 && g_own.on && !g_own.paused && g_own.menu == 0) {
        other = &g_own.editor; // her editor (never in the globals while player 1's copy draws)
    } else if (local == 2 && g_own.swapped && g_own.town_mode == ED_MODE_GEORAMA) {
        other = &g_town_editor; // his, kept aside while her copy draws hers
    }

    // The held part glides to the cursor as DrawPartsCursor draws it (an eighth of the way each pass), on drawing state:
    // the other's part is kept here from frame to frame, not copied afresh from their editor, whose part cursor only the
    // other copy's own drawing moves. Copied afresh, the part stood where they picked it while their hand went on (Jep's
    // playtest, 2026-10-30: her house far behind her hand on his screen). DC_COOP_MUTATE=georama_other_part_reset: as
    // before (twin).
    static float other_part[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    static int   other_held = -1;

    if (other == nullptr) {
        other_held = -1;
        return;
    }

    sceVu0FVECTOR at;
    sceVu0FVECTOR colour = {0.0f, 0.0f, 0.0f, 0.0f};
    float         parts_at[4];

    sceVu0CopyVector(at, (float *) other->frame_pos);

    if (other->select_parts != other_held || Mutated("georama_other_part_reset")) {
        sceVu0CopyVector(other_part, (float *) other->parts_cursor);
        other_held = other->select_parts;
    }

    sceVu0CopyVector(parts_at, other_part);
    colour[1] = other->rot_y;
    at[1] = pEditGround->GetAlt(at[0], at[1], at[2]);
    pEditGround->DrawPartsCursor(other->select_parts, at, parts_at, other->select_angle, colour, pass);
    sceVu0CopyVector(other_part, parts_at);

    // DC_COOP_DEBUG: where the other's held part is drawn against their cursor, once a second (t-p2-georama-on -Case part).
    static int part_said = 0;

    if (pass == 1 && other->select_parts >= 0 && CoopDebug() && ++part_said % 60 == 1) {
        std::printf("coop-georama: p%d drew player %d's part %d at (%.0f %.0f), their cursor (%.0f %.0f)\n", local, 3 - local, other->select_parts,
                    parts_at[0], parts_at[2], at[0], at[2]);
    }

    static const bool debug = CoopDebug();
    static int        said = 0;

    if (debug && pass == 1 && ++said % 60 == 1) {
        std::printf("coop-debug: p%d draws player %d's cursor at (%.0f %.0f), part %d\n", local, 3 - local, at[0], at[2], other->select_parts);
    }
}

bool CoopP2DrawCamera(float (*view)[4], float *eye) {
    if (NetplayLocalPlayer() != 2 || !g_own.on) {
        return false;
    }

    // Her camera for the view only, the edit camera back as it was at once.
    unsigned char kept[sizeof(CCameraFollow)];

    CameraSave(kept);
    CameraLoad(g_own.swapped ? g_own.editor.camera : g_own.editor.camera);
    EditCamera.GetCameraMatrix(view);
    EditCamera.GetPos(eye);
    CameraLoad(kept);
    return true;
}

void CoopP2DrawEditorEnd() {
    // Her menu's still, once her fourth opening frame is drawn (her copy, her screen).
    if (NetplayLocalPlayer() == 2 && g_own.still_wanted && !g_own.still_kept && !Mutated("p2georama_early_still")) {
        if (CTexture *still = TexManager.GetTexture("frame_image", -1)) {
            EdSaveFrameImage(*still);
            g_own.still_kept = true;
            std::printf("coop-georama: her menu's still kept after her frame was drawn\n");
        }
    }

    // The glide this frame drew, kept for the next (the editor itself goes back below).
    bool building = NetplayLocalPlayer() == 2 ? g_own.on : GameMode == ED_MODE_GEORAMA;

    if (building && g_draw_saved) {
        sceVu0CopyVector(g_hand, NowCursorPos);
        g_hand_valid = true;
    }
    if (building && g_draw_saved) {
        sceVu0CopyVector(g_part_glide, NowPartsCursorPos);
        g_part_held = NowSelectParts;
        g_part_valid = true;
        // The rows' reading (t-triangle-menu -Case townmove): how far the part this copy's player holds stood from the cursor this frame.
        static int part_frames = 0;

        if (NowSelectParts >= 0 && CoopDebug() && ++part_frames % 30 == 1) {
            const float dx = NowPartsCursorPos[0] - ECursorFrame->position[0], dz = NowPartsCursorPos[2] - ECursorFrame->position[2];

            std::fprintf(stderr, "coop-georama: p%d's held part %.0f from its cursor (link tick %lld)\n", NetplayLocalPlayer(), std::sqrt(dx * dx + dz * dz), (long long) NetplayLinkTick());
        }
    } else {
        g_part_valid = false;
    }

    // Player 1's editor back; hers stays as her step left it (what the drawing touched, her camera among it, is this
    // copy's alone, and the next steps must read the same editor on both copies).
    if (g_own.swapped) {
        LoadEditor(g_town_editor);
        g_own.swapped = false;
    }

    if (g_draw_saved) {
        LoadEditor(g_draw_editor);
        g_draw_saved = false;
    }
}

// coop-pt1108 (Jep's 11-08 playtest: picking a part up on the battle menu's Georama Parts page: OUT OF SYNC, the part gone): the Atla page, in the battle menu
// or the parts menu, writes the town's chip board (SaveData elem_data) and the parts' elements (EditPartsInfo), retail lockstep state, and a local menu steps on one copy. Every word its step changes in the
// save or the parts' records goes out as a poke, as coop_fullmenu's LocalWrites does the party status (which runs inside this, so its words are back by here): the
// menu sees its own writes at once (the overlay), the world the same on every copy till the poke applies, on one tick. Its bytes alone, two chips share a word and
// both players may have the page open. DC_COOP_MUTATE=partswrites_here (twin): they stay on the menu's copy, as before.
namespace {
struct PartsPoke {
    std::uint32_t word;
    std::uint8_t  bytes;
    std::uint32_t value;
};

std::vector<PartsPoke> g_parts_pokes;

// The save's Georama records and chip boards (georama[6], then elem_data, side by side in CSaveData), then the live parts' records.
constexpr std::size_t kSaveWords = (6 * sizeof(SV_GEORAMA_DATA) + 6 * 128 * sizeof(s16)) / 4;
constexpr std::size_t kPartsWords = sizeof(EditPartsInfo.parts) / 4;
static_assert(kSaveWords + kPartsWords < 0x10000);

std::uint32_t *PartsWord(std::size_t word) {
    return word < kSaveWords ? reinterpret_cast<std::uint32_t *>(reinterpret_cast<unsigned char *>(SaveData->GetElemData(0)) - 6 * sizeof(SV_GEORAMA_DATA)) + word
                             : reinterpret_cast<std::uint32_t *>(EditPartsInfo.parts) + (word - kSaveWords);
}

void PartsPokeOn(std::uint32_t *to, std::uint8_t bytes, std::uint32_t value) {
    auto *dst = reinterpret_cast<unsigned char *>(to);
    auto *src = reinterpret_cast<const unsigned char *>(&value);

    for (int i = 0; i < 4; i++) {
        if ((bytes & (1 << i)) != 0) {
            dst[i] = src[i];
        }
    }
}

class PartsWrites {
  public:
    explicit PartsWrites(bool collect) : collect_(collect) {
        if (SaveData == nullptr || coop::Mutated("partswrites_here")) {
            return;
        }
        on_ = true;
        live_.resize(kSaveWords + kPartsWords);
        Copy(live_.data());
        for (const PartsPoke &poke : g_parts_pokes) {
            PartsPokeOn(PartsWord(poke.word), poke.bytes, poke.value);
        }
        before_.resize(live_.size());
        Copy(before_.data());
    }
    ~PartsWrites() {
        if (!on_) {
            return;
        }
        std::vector<NetCommand> commands;

        for (std::size_t i = 0; collect_ && i < live_.size(); i++) {
            const std::uint32_t now = *PartsWord(i);

            if (now == before_[i]) {
                continue;
            }
            std::uint8_t bytes = 0;

            for (int b = 0; b < 4; b++) {
                if (reinterpret_cast<const unsigned char *>(&now)[b] != reinterpret_cast<const unsigned char *>(&before_[i])[b]) {
                    bytes |= (std::uint8_t) (1 << b);
                }
            }
            NetCommand command;

            command.kind = kCmdPartsPoke;
            command.a = bytes;
            command.b = (std::uint16_t) i;
            command.c = now;
            commands.push_back(command);
            g_parts_pokes.push_back(PartsPoke{(std::uint32_t) i, bytes, now});
        }
        for (std::size_t i = 0; i < live_.size(); i++) {
            *PartsWord(i) = live_[i];
        }
        for (const NetCommand &command : commands) { // (after the put-back: a catch-up's sender applies one at once)
            NetplayQueueCommand(command);
        }
    }
    PartsWrites(const PartsWrites &) = delete;
    PartsWrites &operator=(const PartsWrites &) = delete;

  private:
    static void Copy(std::uint32_t *to) {
        for (std::size_t i = 0; i < kSaveWords + kPartsWords; i++) {
            to[i] = *PartsWord(i);
        }
    }
    bool                       collect_;
    bool                       on_ = false;
    std::vector<std::uint32_t> live_;
    std::vector<std::uint32_t> before_;
};

std::optional<PartsWrites> g_parts_writes;

void ApplyPartsPoke(int player, const NetCommand &command, std::int64_t tick) {
    if (SaveData == nullptr || command.b >= kSaveWords + kPartsWords) {
        return;
    }
    PartsPokeOn(PartsWord(command.b), command.a, command.c);
    if (player == NetplayLocalPlayer()) {
        auto it = std::find_if(g_parts_pokes.begin(), g_parts_pokes.end(), [&](const PartsPoke &p) { return p.word == command.b && p.bytes == command.a && p.value == command.c; });

        if (it != g_parts_pokes.end()) {
            g_parts_pokes.erase(it);
        }
    }
    std::fprintf(stderr, "coop-georama: player %d's parts menu write: word %d bytes %x = %08x at link tick %lld\n", player, (int) command.b, (unsigned) command.a, (unsigned) command.c,
                 (long long) tick);
}
} // namespace

void CoopPartsWritesBegin(bool collect) {
    coop::WriteGuardEnter(coop::kGuardMenu, "FullMenuWritesBegin"); // coop-review1109 (a test's: DC_QA_WRITE_GUARD)
    g_parts_writes.emplace(collect);
}

void CoopPartsWritesEnd() {
    g_parts_writes.reset();
    coop::WriteGuardLeave(coop::kGuardMenu); // coop-review1109
}

// coop-pt1109c (Jep 11-09, item 2): retail re-places every villager as build mode ends (EdInitVilagerPosition), a teleport for whoever watches the town
// walk on. In co-op only those a new part blocks move; everyone else keeps walking, the same on both copies, whoever built. Blocked: on any town tick
// with a Georama open, the spot a villager stood on was clear of parts (InsidePart, the test that puts a player out of a new part: any part but a road) and the
// same spot is inside one a tick later. Comparing one spot across a tick is what tells a part going down on a villager from a villager walking onto
// a part, and it sees any builder and any way a part arrives. As a Georama ends: the blocked go where retail's placement sends a walker (GetRandomPlanePos near their
// part, widening; clear of the players and other walkers, 200 away; failing that their people spot); one whose people spot moved with its part follows it;
// one a part brought in or took away shows or hides by retail's rule. Alone, retail's own call. DC_COOP_MUTATE=villagers_all_move (twin): retail's call, all placed
// again; villagers_never_move (twin): the blocked stay inside.
namespace {
struct VillagersWatch {
    std::uint32_t blocked;
    int           valid;
    float         at[10][3];
    int           clear[10];
};
VillagersWatch g_villagers_watch{}; // (state set: a resync with a Georama open keeps it)
std::uint32_t  g_villagers_test_drawn = 0; // DC_COOP_TEST_VILLAGERS_BLOCK's villagers drawn for the test (no house up in a fixture's Norune)
long long      g_villagers_said_at = -1;   // the test's look 120 ticks after a Georama's end

bool VillagerOnShow(int i) {
    return EdVillager[i].initialized != 0 && EdVillager[i].draw_enabled != 0;
}

void VillagersSay(const char *when) {
    for (int i = 0; i < 10; i++) {
        if (VillagerOnShow(i)) {
            sceVu0FVECTOR at;

            EdVillager[i].GetPosition(at);
            std::fprintf(stderr, "coop-georama: villager %d %s at (%.2f %.2f %.2f) walks %d, link tick %lld\n", i, when, at[0], at[1], at[2], (int) EdVillager[i].event_status,
                         (long long) NetplayLinkTick());
        }
    }
    std::fflush(stderr);
}

void VillagersWatchStep() {
    if (g_villagers_said_at >= 0 && NetplayLinkTick() >= g_villagers_said_at) {
        g_villagers_said_at = -1;
        VillagersSay("120 ticks on");
    }
    if (pEditGround == nullptr) {
        return;
    }
    if (!TownBuilds() && !g_own.on) {
        g_villagers_watch.valid = 0;
        return;
    }
    for (int i = 0; i < 10; i++) {
        if (g_villagers_watch.valid != 0 && g_villagers_watch.clear[i] != 0 && (g_villagers_watch.blocked & (1u << i)) == 0 && VillagerOnShow(i) &&
            InsidePart(g_villagers_watch.at[i])) {
            g_villagers_watch.blocked |= 1u << i;
            std::fprintf(stderr, "coop-georama: a part went down on villager %d at (%.2f %.2f), link tick %lld\n", i, g_villagers_watch.at[i][0], g_villagers_watch.at[i][2],
                         (long long) NetplayLinkTick());
        }

        sceVu0FVECTOR at = {0.0f, 0.0f, 0.0f, 1.0f};

        if (VillagerOnShow(i)) {
            EdVillager[i].GetPosition(at);
        }
        g_villagers_watch.at[i][0] = at[0];
        g_villagers_watch.at[i][1] = at[1];
        g_villagers_watch.at[i][2] = at[2];
        g_villagers_watch.clear[i] = VillagerOnShow(i) && !InsidePart(at);
    }
    g_villagers_watch.valid = 1;
}

// Test hook (DC_COOP_TEST_VILLAGERS_BLOCK=1): while a builder holds a part (their cursor is the globals' here), every villager set up drawn, and the first
// stood on the cursor till a part goes down, so the part lands on it; both copies alike.
void VillagersBlockForTest(int placed_at_start) {
    static const bool test = TestEnvOn("DC_COOP_TEST_VILLAGERS_BLOCK");
    static bool       done = false;

    if (!test || done || NowSelectParts < 0 || ECursorFrame == nullptr) {
        return;
    }
    if (PlacedParts() > placed_at_start) {
        done = true;
        return;
    }

    bool first = true;
    bool walker = false;

    for (int i = 0; i < 10; i++) {
        if (EdVillager[i].initialized == 0 || EdVillager[i].villager_id < 0) {
            continue;
        }
        if ((g_villagers_test_drawn & (1u << i)) == 0) {
            g_villagers_test_drawn |= 1u << i;
            EdVillager[i].draw_enabled = true;
            EdVillager[i].ClearSeq(); // (hidden till now, its walk not begun)
            EdVillager[i].SetMotion(0, 0);
        }
        if (first) {
            first = false;
            sceVu0FVECTOR at;

            sceVu0CopyVector(at, ECursorFrame->position);
            at[3] = 1.0f;
            EdVillager[i].SetPosition(at);
        } else if (!walker && EdVillager[i].event_status != 0 && Chara != nullptr) {
            walker = true; // (retail steps a villager's walk only near the camera: one walker by Toan, to see it walk on)

            sceVu0FVECTOR at;

            Chara->GetPosition(at);
            at[0] += 180.0f; // (clear of the part the builder holds)
            at[2] += 180.0f;
            EdVillager[i].SetPosition(at);
        }
    }
}

// Retail's rule for whether a villager shows (EdInitVilagerPosition's last loop).
bool VillagerShows(const VILLAGER_INFO &entry, int i) {
    if ((g_villagers_test_drawn & (1u << i)) != 0) {
        return true;
    }
    if (entry.character_no < 0) {
        return true;
    }
    if (pEditGround->GetPartsObject(entry.character_no) == nullptr) {
        return false;
    }

    EDITPARTS_INFO *parts = EditPartsInfo.GetPartsInfo(entry.character_no);
    bool            draw = parts->obtained > 0 && parts->elements[entry.model_no].enabled != 0;

    if (EditPartsInfo.CheckComplete(entry.character_no) != 0 && EditPartsInfo.GetCompEvent(entry.character_no) == 0 && entry.hide_when_complete != 0) {
        draw = false;
    }
    return draw;
}

// Retail's walker placement for one villager (EdInitVilagerPosition's moving loop).
bool VillagerWalkerSpot(int selected, const float *toan, float *out) {
    sceVu0FVECTOR avoid[12];
    int           count = 0;

    sceVu0CopyVector(avoid[count], (float *) toan);
    count++;

    sceVu0FVECTOR her = {0.0f, 0.0f, 0.0f, 1.0f};

    if (coop::TownPartnerShown(her, nullptr)) {
        sceVu0CopyVector(avoid[count], her);
        count++;
    }
    for (int i = 0; i < 10; i++) {
        if (i != selected && VillagerOnShow(i) && EdVillager[i].event_status != 0) {
            EdVillager[i].GetPosition(avoid[count]);
            count++;
        }
    }
    for (int i = 0; i < count; i++) {
        avoid[i][3] = 200.0f;
    }

    sceVu0FVECTOR reference;

    reference[3] = -1.0f;

    if (auto *object = pEditGround->GetPartsObject(EdVillagerInfo[selected].character_no); object != nullptr) {
        sceVu0CopyVector(reference, object->pos);
        reference[3] = 200.0f;
    }
    for (;;) {
        if (pEditGround->GetRandomPlanePos(out, avoid, count, reference) != 0) {
            return true;
        }
        if (reference[3] > 500.0f) {
            reference[3] = -1.0f;
            return pEditGround->GetRandomPlanePos(out, avoid, count, reference) != 0;
        }
        reference[3] += 100.0f;
    }
}

void VillagerPut(int i, const float *position, const float *rotation, int walks, bool draw) {
    VILLAGER_INFO &entry = EdVillagerInfo[i];
    sceVu0FVECTOR  to;
    sceVu0FVECTOR  turn;

    sceVu0CopyVector(to, (float *) position);
    sceVu0CopyVector(turn, (float *) rotation);
    to[3] = 1.0f;
    sceVu0CopyVector(entry.position, to);
    sceVu0CopyVector(entry.rotation, turn);
    entry.placed = true;
    entry.initial_motion = walks;
    EdVillager[i].ClearSeq();
    EdVillager[i].event_status = walks;
    EdVillager[i].villager_id = entry.index;
    EdVillager[i].SetPosition(to);
    EdVillager[i].SetRotation(turn[0], turn[1], turn[2]);
    EdVillager[i].draw_enabled = draw;
    EdVillager[i].SetMotion(0, 0);
}
} // namespace

void CoopGeoramaLeaveVillagers(float (*at)[4]) {
    VillagersWatchStep(); // (a part down on this very tick)

    if (!NetplayHasPlayer() || MapNo > 10 || Mutated("villagers_all_move")) {
        EdInitVilagerPosition(EdVillager, EdVillagerInfo, pEditGround, at);
        std::fprintf(stderr, "coop-georama: villagers as the Georama ends: retail's placement, all of them, link tick %lld\n", (long long) NetplayLinkTick());
        g_villagers_watch.blocked = 0;
        return;
    }

    // (the test: a walk under way on the walker by Toan as the Georama ends, to see that it goes on: retail's call clears it)
    if (TestEnvOn("DC_COOP_TEST_VILLAGERS_BLOCK")) {
        for (int i = 0; i < 10; i++) {
            if (VillagerOnShow(i) && EdVillager[i].event_status != 0 && (g_villagers_watch.blocked & (1u << i)) == 0) {
                sceVu0FVECTOR to;

                EdVillager[i].GetPosition(to);
                to[0] += 100.0f;
                EdVillager[i].ClearSeq();
                EdVillager[i].SetSeq(to, 1.0f);
                break;
            }
        }
    }

    VillagersSay("before the Georama's end");

    const int random_state = rand(); // (as retail: the placement's draws put back)
    int       moved = 0;
    int       kept = 0;

    for (int i = 0; i < 10; i++) {
        VILLAGER_INFO &entry = EdVillagerInfo[i];

        if (entry.name[0] == '\0' || entry.index < 0 || EdVillager[i].initialized == 0) {
            continue;
        }

        const bool    shown = EdVillager[i].draw_enabled != 0;
        const bool    shows = VillagerShows(entry, i);
        const bool    blocked = (g_villagers_watch.blocked & (1u << i)) != 0 && !Mutated("villagers_never_move");
        const bool    walks = EdVillager[i].event_status != 0;
        sceVu0FVECTOR people = {0.0f, 0.0f, 0.0f, 1.0f};
        sceVu0FVECTOR now = {0.0f, 0.0f, 0.0f, 1.0f};
        const bool    has_spot = pEditGround->GetPeoplePos(entry.index, people) != nullptr;
        const float   spot_yaw = people[3];

        people[3] = 1.0f;
        EdVillager[i].GetPosition(now);

        if (!shows) {
            if (shown) {
                EdVillager[i].draw_enabled = false;
                moved++;
                std::fprintf(stderr, "coop-georama: villager %d hidden (its part went)\n", i);
            }
            continue;
        }

        const sceVu0FVECTOR turn = {0.0f, has_spot ? spot_yaw : entry.rotation[1], 0.0f, 0.0f};
        const bool          spot_moved = !walks && has_spot && (std::fabs(people[0] - now[0]) > 1.0f || std::fabs(people[2] - now[2]) > 1.0f);

        if (!blocked && shown && !spot_moved) {
            kept++;
            continue;
        }
        if (spot_moved && !blocked) {
            VillagerPut(i, people, turn, 0, true);
        } else {
            float to[4];

            if (VillagerWalkerSpot(i, *at, to)) {
                VillagerPut(i, to, entry.rotation, 1, true);
            } else if (has_spot) {
                VillagerPut(i, people, turn, 0, true);
            } else {
                VillagerPut(i, now, entry.rotation, 0, false);
            }
        }
        moved++;

        sceVu0FVECTOR after;

        EdVillager[i].GetPosition(after);
        std::fprintf(stderr, "coop-georama: villager %d moved (%s) from (%.2f %.2f) to (%.2f %.2f), %s, link tick %lld\n", i,
                     blocked ? "a new part blocked it" : shown ? "its spot moved" : "newly shown", now[0], now[2], after[0], after[2],
                     InsidePart(after) ? "inside a part" : "clear of parts", (long long) NetplayLinkTick());
    }

    srand(random_state);
    g_villagers_watch.blocked = 0;
    std::fprintf(stderr, "coop-georama: villagers as the Georama ends: %d moved, %d kept walking, link tick %lld\n", moved, kept, (long long) NetplayLinkTick());
    VillagersSay("after the Georama's end");
    g_villagers_said_at = NetplayLinkTick() + 120;
}

// Around player 1's step in his own Georama (the town's mode step): his Cross with a part held off her cell.
void CoopGeoramaP1StepBegin() {
    CoopTownClockStepBegin(); // coop-pt1108c
    VillagersWatchStep();     // coop-pt1109c
    {
        static int builder_placed_a = -1;

        if (TownBuilds() && !g_own.on) {
            builder_placed_a = builder_placed_a < 0 ? PlacedParts() : builder_placed_a;
            VillagersBlockForTest(builder_placed_a);
        }
    }
    CoopTownTickView(); // coop-localfix: the tick's view from the game's camera (each town tick starts here)
    CoopTownListenerTick(); // coop-pt1108
    // DC_COOP_TEST_DUSK_AT=<link tick> (t-triangle-menu -Case townbattledusk): the clock to just before dusk, the time of day's event on its way, every copy alike.
    static const int test_dusk = TestEnvInt("DC_COOP_TEST_DUSK_AT", -1);
    static bool      test_dusk_done = false;

    if (test_dusk >= 0 && !test_dusk_done && NetplayLinkTick() >= test_dusk) {
        test_dusk_done = true;
        // DC_COOP_TEST_DUSK_CLOCK=<tenths> (coop-pt1108f): another hour, 54 for 5.4999, the hour of Jep's 11-09 day card
        const float clock = TestEnvInt("DC_COOP_TEST_DUSK_CLOCK", 24) / 10.0f + 0.0999f;

        NowTime = clock;
        SaveData->SetNowTime(clock);
        change_time_event = 1;
        std::fprintf(stderr, "coop-georama: test: the clock to %.4f at link tick %lld\n", clock, (long long) NetplayLinkTick());
    }
    // DC_COOP_TEST_PARTS_CHIP=<id> (t-triangle-menu -Case townpartspick): that chip in the board's first 20 slots on the first tick here, as if found.
    static const int test_chip = TestEnvInt("DC_COOP_TEST_PARTS_CHIP", -1);
    static bool      test_chip_done = false;

    if (test_chip >= 0 && !test_chip_done && SaveData != nullptr && CoopTownMenuLinked() && NetplayLinkTick() >= 65) { // (past the fixture's load, under the first press)
        test_chip_done = true;
        // (and the catalogue's first part in stock, its first element's chip what the board holds: the whole flow's row)
        int chip = test_chip;

        for (int i = 0; i < EditPartsInfo.parts_max && i < 24; i++) {
            EDITPARTS_INFO *info = EditPartsInfo.GetPartsInfo(i);

            if (info != nullptr && info->header != nullptr && info->width > 0) {
                info->obtained = 1;
                info->stock = info->placed + 2;
                chip = info->elements[0].id >= 0 ? info->elements[0].id : chip;
                for (int e = 0; e < 6; e++) {
                    info->elements[e].enabled = 0;
                }
                break;
            }
        }
        if (s16 *board = SaveData->GetElemData(NowEditMap)) {
            for (int i = 0; i < 20; i++) {
                board[i] = (s16) chip;
            }
        }
        std::fprintf(stderr, "coop-georama: test: chip %d on the board, a part in stock at link tick %lld\n", chip, (long long) NetplayLinkTick());
    }
    if (GameMode != ED_MODE_GEORAMA_MENU && !(g_own.on && g_own.menu == 2) && !coop::PlayerInMenu(NetplayLocalPlayer(), coop::kActorMenuLocal)) {
        g_parts_pokes.clear(); // coop-pt1108: no menu open here (a resync's lost ones with it)
    }
    if (GameMode != ED_MODE_GEORAMA_MENU) {
        g_his_parts_sent = false; // (no menu open: nothing left of a close from before a resync or a park)
        g_his_parts_shut = false;
        g_his_parts_ticks = 0;
    }
    if (test_chip >= 0 && pEditGround != nullptr) { // (the flow's row reads the map's parts on each copy)
        static int placed_said = -1;
        const int  placed_now = PlacedParts();

        if (placed_now != placed_said) {
            placed_said = placed_now;
            std::fprintf(stderr, "coop-georama: test: placed parts %d at link tick %lld\n", placed_now, (long long) NetplayLinkTick());
        }
    }
    g_p1_placed = GameMode == ED_MODE_GEORAMA && pEditGround != nullptr ? PlacedParts() : -1;
    g_under_before = g_p1_placed >= 0 ? UnderPlayer(2) : nullptr;

    // Her part in hand counted placed through his step (her editor is never in the globals here).
    if (g_own.on && g_own.editor.select_parts >= 0 && EditPartsInfo.parts_max > 0) {
        Reserve(g_own.editor.select_parts);
    }
}

void CoopGeoramaP1StepEnd() {
    Unreserve();

    // DC_COOP_DEBUG: player 1's edit camera (its eye) once a second while he builds, for the tests.
    static int camera_ticks = 0;

    if (GameMode == ED_MODE_GEORAMA && CoopDebug() && ++camera_ticks % 60 == 0) {
        sceVu0FVECTOR eye;

        EditCamera.GetPos(eye);
        std::printf("coop-debug: p1 edit camera eye (%.1f %.1f %.1f), her Georama %d\n", eye[0], eye[1], eye[2], (int) g_own.on);
    }

    // A part player 1 put down in his own Georama: her character out of it.
    if (g_p1_placed >= 0 && PutOn(2, g_p1_placed, g_under_before) && NetplayLocalPlayer() != 0) {
        PushOut(2);
    }

    g_p1_placed = -1;
}

bool CoopP2MenuOpen() {
    return g_own.on && g_own.menu != 0;
}

bool CoopP2MenuHoldsP1(int menu_no) {
    if (!CoopP2MenuOpen()) {
        return false;
    }
    // (both local: her parts menu on her copy alone, his menu on his alone; neither makes the other's on its copy)
    return !(PartsMenuLocal() && CoopTownP1MenuRunsLocal(menu_no)) || Mutated("p2georama_holds_his_menu");
}

bool CoopP2MenuHoldsParts() {
    return CoopP2MenuOpen() && (!PartsMenuLocal() || !HisPartsLocal() || Mutated("p1parts_one_at_a_time"));
}

bool CoopP1PartsMenuElsewhere() {
    return HisPartsLocal() && NetplayLocalPlayer() != kHisPlayer;
}

bool CoopPartsMenuLocalOpen() {
    return (PartsMenuLocal() && g_own.on && g_own.menu == 2) || (HisPartsLocal() && (g_own.swapped ? g_own.town_mode : (int) GameMode) == ED_MODE_GEORAMA_MENU);
}

bool CoopP1PartsMenuShutHere() {
    return g_his_parts_sent && GameMode == ED_MODE_GEORAMA_MENU;
}

// coop-pt1108 (Jep's 11-08 playtest: "when P1 closes it, both players' screens flash"): the tick his local parts menu shuts on every copy, retail's step returned before
// the draw (its menu had drawn that tick; local, nothing has): the frame draws the town. As coop-menuflash for his battle menu. DC_COOP_MUTATE=partsclose_flash (twin): nothing, as before.
bool CoopP1PartsMenuCloseFrame() {
    if (!HisPartsLocal() || coop::Mutated("partsclose_flash")) {
        return false;
    }
    std::fprintf(stderr, "coop-georama: player 1's parts menu's close tick draws the town (link tick %lld)\n", (long long) NetplayLinkTick());
    return true;
}

int CoopP1PartsMenuStep() {
    if (!HisPartsLocal()) {
        return EditMenuLoop();
    }
    // DC_COOP_DEBUG: where she stands under it, every copy (the tests read her walk).
    static int walk_ticks = 0;
    float      at[4] = {0.0f, 0.0f, 0.0f, 1.0f};

    if (CoopDebug() && ++walk_ticks % 30 == 1 && coop::TownPartnerShown(at, nullptr)) {
        std::printf("coop-georama: his parts menu open, player 2 at (%.1f %.1f)\n", at[0], at[2]);
    }
    if (NetplayLocalPlayer() == kHisPlayer && !g_his_parts_sent) {
        int shut = 0;
        {
            WriteGuardMenu guard("his parts menu"); // coop-guard (a test's: DC_QA_WRITE_GUARD)
            DrawRng        rng; // its random numbers his copy's own
            // coop-guard (the write guard's townparts row: GamePad+0x145 on his copy alone): the town's step opened his parts menu on every
            // copy (EdInitMenu: menu mode, EditMenuInit: the d-pad's repeat, in the game pad), but its way out (EditMenuExit: repeat and
            // menu mode off) ran in his step, on his copy alone, and the others kept them: CGamePad::UpDate applies the repeat to port
            // 1's agreed buttons, the ones the world reads. So the game pad is the world's here, as it was, and the way out's pad part
            // runs on every copy with his close command (ApplyPartsMenuClose). DC_COOP_MUTATE=partspad_here (twin): his copy alone, as before.
            const CGamePad world_pad = GamePad;

            FullMenuStepping(kHisPlayer);
            FullMenuWritesBegin(true);
            shut = EditMenuLoop();
            FullMenuWritesEnd();
            FullMenuStepping(0);
            if (!Mutated("partspad_here")) {
                GamePad = world_pad;
            }
        }
        if (shut != 0) {
            NetCommand command;
            command.kind = kCmdGeoMenu;
            command.a = (std::uint8_t) EditMenuStatus.mode;
            command.b = (std::uint16_t) (EditMenuStatus.parts + 1);
            command.c = (std::uint32_t) EditMenuStatus.event_no;
            g_his_parts_sent = true;
            std::printf("coop-georama: his local parts menu shut on his copy (mode %d, part %d, event %d): its close command sent\n", EditMenuStatus.mode,
                        EditMenuStatus.parts, EditMenuStatus.event_no);
            NetplayQueueCommand(command); // (a recorded catch-up tick applies it now: below)
        } else if (g_his_parts_ticks++ == 5) {
            std::printf("coop-georama: his local parts menu steps on his copy alone\n");
        }
    }
    if (!g_his_parts_shut) {
        return 0;
    }
    g_his_parts_shut = false;
    g_his_parts_sent = false;
    g_his_parts_ticks = 0;
    return 1;
}

// coop-stage4: her local parts menu's close (kCmdGeoMenu), on every copy on its tick: what it handed back into her editor (on her
// copy the menu wrote it already), her menu shut, her pad out of the menu mode, and its event held as the lockstep's close held it.
// coop-guard: the game pad's port 1 repeat bits and menu threshold, for the close's line (CGamePad keeps them private: gamepad.hpp's
// layout, its size asserted there: repeat[0].enabled at 0x144, axis_threshold[0] after both repeats at 0x454).
static std::uint32_t GamePadRepeatBits() {
    std::uint32_t bits = 0;

    std::memcpy(&bits, reinterpret_cast<const unsigned char *>(&GamePad) + 0x144, sizeof bits);
    return bits;
}

static int GamePadMenuThreshold() {
    int threshold = 0;

    std::memcpy(&threshold, reinterpret_cast<const unsigned char *>(&GamePad) + 0x454, sizeof threshold);
    return threshold;
}

static void ApplyPartsMenuClose(int player, const NetCommand &command, std::int64_t tick) {
    if (player == kHisPlayer) {
        // coop-p1parts: his: what it handed back on this copy too; retail's close runs in the town's step.
        if ((g_own.swapped ? g_own.town_mode : (int) GameMode) != ED_MODE_GEORAMA_MENU) {
            std::printf("coop-georama: player 1's parts menu close command at link tick %lld finds no menu open: dropped\n", (long long) tick);
            return;
        }
        EDIT_MENU_STATUS &his = g_own.swapped ? g_town_editor.menu_status : EditMenuStatus;
        his.mode = (std::int8_t) command.a; // (coop-pt1108: closed is -1)
        his.parts = (int) command.b - 1;
        his.event_no = (int) command.c;
        g_his_parts_shut = true;
        // coop-guard: retail's way out of the parts menu for the game pad (editmenu.cpp EditMenuExit), on every copy on this tick: the
        // open put them on every copy (EdInitMenu). DC_COOP_MUTATE=partspad_here (twin): his copy alone did, in his step.
        if (!Mutated("partspad_here")) {
            GamePad.AutoRepeatOff();
            GamePad.MenuModeOff();
        }
        std::printf("coop-georama: player 1's parts menu close leaves the game pad: repeat %08x, menu mode %d (link tick %lld)\n", (unsigned) GamePadRepeatBits(),
                    GamePadMenuThreshold(), (long long) tick);
        std::printf("coop-georama: player 1's parts menu shuts, mode %d, part %d (by its command, link tick %lld)\n", his.mode, his.parts, (long long) tick);
        return;
    }
    if (!g_own.on || g_own.menu != 2) {
        std::printf("coop-georama: player %d's parts menu close command at link tick %lld finds no menu open: dropped\n", player, (long long) tick);
        return;
    }

    EDIT_MENU_STATUS &status = g_own.swapped ? EditMenuStatus : g_own.editor.menu_status;
    status.mode = (std::int8_t) command.a; // (coop-pt1108: closed is -1)
    status.parts = (int) command.b - 1;
    status.event_no = (int) command.c;
    const int part = status.mode == EDIT_MENU_MODE_PLACE ? status.parts : -1;
    (g_own.swapped ? NowSelectParts : g_own.editor.select_parts) = part;
    g_own.menu = 0;
    g_own.menu_sent = false;
    g_own.pad.AutoRepeatOff();
    g_own.pad.MenuModeOff();

    if (status.mode == EDIT_MENU_MODE_EVENT) {
        Queue(status.event_no + 0xC8, 0, "her menu");
    } else {
        std::printf("coop-georama: player 2's parts menu shuts, mode %d, part %d (by its command, link tick %lld)\n", status.mode, part, (long long) tick);
    }
}

void CoopP2GeoramaCommandHandler() {
    NetplaySetCommandHandler(kCmdGeoMenu, ApplyPartsMenuClose);
    NetplaySetCommandHandler(kCmdPartsPoke, ApplyPartsPoke); // coop-pt1108
}

int CoopP2DrawnMode() {
    // Her parts menu open: the town's own mode, its frame drawn where nobody sees it (CoopP2TownFrameBegin).
    // DC_COOP_MUTATE=p2georama_menu_town_frame: her copy draws only her menu, as before (twin).
    if (g_own.on && g_own.menu == 2 && Mutated("p2georama_menu_town_frame")) {
        return ED_MODE_GEORAMA_MENU;
    }

    if (g_own.on && g_own.paused) {
        return ED_MODE_RETURN_MENU_GEORAMA; // her pause's dim and sprite, her Georama under it
    }

    return !g_own.on || (g_own.menu == 2 && !g_own.menu_sent) ? -1 : ED_MODE_GEORAMA; // (coop-stage4: her Georama again once her local menu shut)
}

// Her parts menu is her copy's screen (the menu draws as it steps, over the still it keeps), but the town's frame
// steps things as it draws (an event's message windows and sprites, the effects): her copy draws the town's own frame
// too, as player 1's copy does, only into a target nobody sees, so both copies step alike. (2026-10-21: her copy
// drew only her menu while player 1's town played the dusk event, and the copies parted at tick 2100.)
static gfx::TextureHandle g_town_hidden = gfx::kNullTexture;
static gfx::TextureHandle g_town_previous = gfx::kNullTexture;
static bool               g_town_hiding = false;

void CoopP2TownFrameBegin() {
    g_town_hiding = NetplayLocalPlayer() == 2 && g_own.on && g_own.menu == 2 && !g_own.menu_sent && !Mutated("p2georama_menu_town_frame");

    if (!g_town_hiding) {
        return;
    }

    if (g_town_hidden == gfx::kNullTexture) {
        g_town_hidden = gfx::CreateRenderTarget(640, 480, true);
    }

    g_town_previous = gfx::CurrentRenderTarget();
    gfx::SetRenderTarget(g_town_hidden);

    static const bool debug = CoopDebug();
    static int        said = -1;

    if (debug && said != g_own.menu_opens) {
        said = g_own.menu_opens;
        std::printf("coop-debug: p2 town frame drawn hidden behind her parts menu\n");
    }
}

void CoopP2TownFrameEnd() {
    if (g_town_hiding) {
        gfx::SetRenderTarget(g_town_previous);
        g_town_hiding = false;
    }
}

// coop-pond test hook (DC_COOP_TEST_PLACE_PART=TICK:PLOT:X:Z, l-local-play -PondJoin): on the host's TICKth walking tick in a
// town (never on a joiner's copy), plot PLOT put in stock and down at (X, Z) as the Georama puts it, so the town's ground is
// dug there before a joiner takes the host's state (Jep's pond: plot 12 in Norune). TICK 1: before the join's snapshot.
void CoopTownPlotDoorsForTest(int plot); // coop_town.cpp (coop-soakfix3)

static void PlacePartTestTick() {
    static const char *spec = TestEnv("DC_COOP_TEST_PLACE_PART");
    static int         at = -1, plot = -1, ticks = 0;
    static float       x = 0.0f, z = 0.0f;
    static bool        parsed = false, done = false, on_load = false;

    // coop-pt1106-batch (x-town-sign): "loaded:PLOT:X:Z", on both linked copies alike once a state file (the row's fixture) is
    // put back (NetplayStateFileLoaded), the first walking tick after it.
    if (!parsed) {
        parsed = true;
        on_load = spec != nullptr && std::strncmp(spec, "loaded:", 7) == 0;
        if (on_load ? std::sscanf(spec + 7, "%d:%f:%f", &plot, &x, &z) != 3 : spec == nullptr || std::sscanf(spec, "%d:%d:%f:%f", &at, &plot, &x, &z) != 4) {
            at = -1;
        } else if (on_load) {
            at = 0;
        }
    }
    if (on_load) {
        if (at < 0 || done || !NetplayStateFileLoaded() || pEditGround == nullptr || GameMode != ED_MODE_WALK) {
            return;
        }
    } else if (at < 0 || done || (NetplayLinkUp() && NetplayLinkPlayer() == 2) || pEditGround == nullptr || GameMode != ED_MODE_WALK || ++ticks < at) {
        return;
    }
    done = true;
    EDITPARTS_INFO *info = EditPartsInfo.GetPartsInfo(plot);

    if (info == nullptr) {
        std::fprintf(stderr, "coop-georama: test: plot %d is not in this town\n", plot);
        return;
    }
    info->obtained = 1;
    info->stock = info->placed + 1;
    const int slot = pEditGround->SetMapParts(plot, x, 0.0f, z, 0);
    std::fprintf(stderr, "coop-georama: test: plot %d put down at (%.0f %.0f) in slot %d\n", plot, x, z, slot);
    // coop-soakfix3: and where its doors are now in the world (a row walks a player to one)
    CoopTownPlotDoorsForTest(plot);
}

void CoopP2GeoramaTick() {
    coop::SoundOwner sound(2, true); // coop-menuse: her Georama's menu sounds are hers
    InputPadState    pad;

    PlacePartTestTick();

    // coop-stage4 r4: her Georama carried into the merge holds where it is (no step, no close) until the copies are together and
    // she is shown in the linked town, on both copies alike (the set carried the hold); a link that drops meanwhile closes it.
    if (g_own.on && g_own.merge_hold) {
        if (!NetplayLinkUp() || mode != kTownMode || EdInteriorFlag != 0 || MapNo != g_own.map) {
            g_own.merge_hold = false;
            End("closes (its merge did not hold)");
            return;
        }
        if (CoopWorldsApart() || !coop::TownPartnerShown(nullptr, nullptr)) {
            return;
        }
        g_own.merge_hold = false;
        std::printf("coop-georama: player 2's carried Georama steps again at link tick %lld: cursor (%.0f %.0f), part in hand %d\n", NetplayLinkTick(),
                    g_own.editor.frame_pos[0], g_own.editor.frame_pos[2], g_own.editor.select_parts);
    }

    if (!NetplayPlayerPad(2, pad)) {
        if (g_own.on) {
            End("closes (no player 2)");
        }

        return;
    }

    std::uint16_t pressed = pad.buttons & ~g_own.prev;

    g_own.prev = pad.buttons;

    // DC_COOP_TEST_P1_PLACE=1: on player 1's 30th tick in his Georama with player 2 in town, the first part of the
    // map's catalogue in stock and picked in his editor (the globals), for the test that he places it on her.
    p1_ticks = TownBuilds() ? p1_ticks + 1 : 0;

    if (!p1_done && p1_ticks == 30 && TestEnvInt("DC_COOP_TEST_P1_PLACE", 0) != 0 && coop::TownPartnerShown(nullptr, nullptr)) {
        for (int i = 0; i < EditPartsInfo.parts_max && i < 24; i++) {
            EDITPARTS_INFO *info = EditPartsInfo.GetPartsInfo(i);

            if (info != nullptr && info->header != nullptr && info->width > 0) {
                info->obtained = 1;
                info->stock = info->placed + 1;
                EditMenuStatus.mode = EDIT_MENU_MODE_PLACE;
                EditMenuStatus.parts = i;
                NowSelectParts = i;
                p1_done = true;
                std::printf("coop-georama: test: part %d in stock and picked for player 1\n", i);
                break;
            }
        }
    }


    // Her Georama's event starts once player 1 walks with no menu, talk or event of his own (both copies alike):
    // her Georama closes for it, and the town plays it as its own.
    // DC_COOP_MUTATE=p2georama_event_now: at once, whatever player 1 is doing (twin).
    if (g_own.event != 0 &&
        (Mutated("p2georama_event_now") || (GameMode == ED_MODE_WALK && goto_cmp_event == 0 && change_time_event == 0))) {
        if (g_own.on) {
            End("closes (its event starts)");
        }

        int taken = FadeOutToEvent(g_own.event, g_own.event_level);

        std::printf("coop-georama: player 2's Georama event %d starts (the town took it: %d)\n", g_own.event, taken);
        g_own.event = 0;
        g_own.event_level = 0;
        g_own.before = pad;
        return;
    }

    if (g_own.on && (!Allowed() || MapNo != g_own.map)) {
        End("closes (the town changed)");
        return;
    }

    if (!g_own.on) {
        // Beside player 1's Georama too (each has an editor of their own); DC_COOP_MUTATE=p2georama_one_at_a_time: not while he builds.
        if ((pressed & kInputSelect) != 0 && Allowed() && (!TownBuilds() || !Mutated("p2georama_one_at_a_time")) && !CoopP2TownMenuOn() &&
            change_time_event == 0) { // not over her own menu
            Begin();
        }

        g_own.before = pad;
        return;
    }

    g_own.ticks++;

    // DC_COOP_P2GEORAMA_TEST_SHOT=DIR (t-georama-bar): each copy's frame on her Georama's 200th tick to DIR/open-p<N>.png,
    // her bar on hers and none on his, the test's yardstick for the frames after her close.
    // (coop-warmconvert, WS7 round 4: the folder a job's own, read again at each job on a warm pair)
    COOP_TEST_SWITCH(const char *, shot_dir, TestEnv("DC_COOP_P2GEORAMA_TEST_SHOT"));
    if (shot_dir != nullptr && g_own.ticks == 200) {
        static char shot[512];

        std::snprintf(shot, sizeof shot, "%s/open-p%d.png", shot_dir, NetplayLocalPlayer());
        NetplayTestShotTo(shot);
    }

    // Her step, with the town's mode, camera and first port hers; all three back after.
    CGamePad town_pad = GamePad;
    int      town_mode = GameMode;

    g_own.town_mode = town_mode;
    CCamera *town_camera = NowCamera;

    GamePad = g_own.pad;
    SwapIn(); // her editor in the globals (player 1's kept aside)

    // DC_COOP_MUTATE=p2georama_nocross: her Cross never reaches her editor (the placing test's twin).
    if (Mutated("p2georama_nocross")) {
        pad.buttons &= ~kInputCross;
        g_own.before.buttons &= ~kInputCross;
    }

    SetPortOne(GamePad, pad, g_own.before);

    // DC_COOP_P2GEORAMA_TEST_PLACE=1: on her Georama's 60th tick the first part of the map's catalogue in stock and
    // picked, as her parts menu hands it back, for the test that places it with her Cross (both copies alike).
    // =2: again on her 400th tick, for a second part.
    static const int test_place = TestEnvInt("DC_COOP_P2GEORAMA_TEST_PLACE", 0);

    if ((g_own.ticks == 60 || (test_place == 2 && g_own.ticks == 400)) && g_own.menu == 0 && test_place != 0) {
        for (int i = 0; i < EditPartsInfo.parts_max && i < 24; i++) {
            EDITPARTS_INFO *info = EditPartsInfo.GetPartsInfo(i);

            if (info != nullptr && info->header != nullptr && info->width > 0) {
                info->obtained = 1;
                info->stock = info->placed + 1;
                EditMenuStatus.mode = EDIT_MENU_MODE_PLACE;
                EditMenuStatus.parts = i;
                NowSelectParts = i;
                std::printf("coop-georama: test: part %d (no %d) in stock and picked, %d placed\n", i, info->parts_no, info->placed);
                break;
            }
        }
    }

    // His part in hand counted placed through her step (his editor kept aside while hers is in).
    if (TownBuilds() && g_town_editor.select_parts >= 0) {
        Reserve(g_town_editor.select_parts);
    }

    GameMode = g_own.menu == 2 ? ED_MODE_GEORAMA_MENU : ED_MODE_GEORAMA;
    NowCamera = (CCamera *) &EditCamera;

    bool          done = false;
    bool          walkable = false;
    sceVu0FVECTOR at = {0.0f, 0.0f, 0.0f, 1.0f};

    // Her own pause at her Georama (Start; DC_COOP_MUTATE=p2georama_nopause: none, the twin). Player 1's world goes on.
    if (g_own.menu == 0 && (pressed & kInputStart) != 0 && !Mutated("p2georama_nopause")) {
        g_own.paused = !g_own.paused;
        Log(g_own.paused ? "pauses" : "goes on");
    }

    if (g_own.menu == 0 && g_own.paused) {
        // Paused: her editor holds (no cursor, camera, menu or Select).
    } else if (g_own.menu == 0) {
        // Every area in, as build mode has it (the cursor's ground and CheckEditToWalk pick only areas in).
        pEditGround->EditAreaClip(NowCamera, -1.0f);
        int              placed_before = PlacedParts();
        const CMapParts *under_before = UnderPlayer(1);

        { // coop-pt1109c: test hook (her cursor is the globals' here)
            static int builder_placed_b = -1;

            builder_placed_b = builder_placed_b < 0 ? placed_before : builder_placed_b;
            VillagersBlockForTest(builder_placed_b);
        }

        EditMode();
        EditCamera.Step(1);

        // A part she put down: Toan out of it while he walks the town (in his own Georama he is not in the world).
        const bool p1_builds = town_mode == ED_MODE_GEORAMA || town_mode == ED_MODE_GEORAMA_MENU_INIT || town_mode == ED_MODE_GEORAMA_MENU ||
                               town_mode == ED_MODE_RETURN_MENU_GEORAMA;

        if (PlacedParts() > placed_before) {
            std::printf("coop-georama: her part went down (%d placed); player 1 %s, %s; her cursor (%.0f %.0f)\n", PlacedParts(),
                        p1_builds ? "builds" : "walks", under_before != nullptr ? "on a part" : "on the ground", ECursorFrame->position[0],
                        ECursorFrame->position[2]);
        }

        if (PutOn(1, placed_before, under_before)) {
            if (town_mode != ED_MODE_GEORAMA && town_mode != ED_MODE_GEORAMA_MENU_INIT && town_mode != ED_MODE_GEORAMA_MENU &&
                town_mode != ED_MODE_RETURN_MENU_GEORAMA) {
                PushOut(1);
            } else {
                std::printf("coop-georama: a part went down where Toan stood while player 1 builds (he is not in the world)\n");
            }
        }

        // Her parts menu on Triangle or Circle, as build mode's, unless a menu of the town's is open (it waits).
        if ((pressed & (kInputTriangle | kInputCircle)) != 0 && !Mutated("p2georama_nomenu")) {
            if (HisPartsMenuOnHerEngine(town_mode)) {
                Log("menu waits (a town menu is open)");
            } else {
                NowSelectParts = -1;
                g_own.menu = 1;
                g_own.menu_ticks = 0;
                Log("menu opens");
            }
        }

        done = g_own.menu == 0 && (pressed & kInputSelect) != 0 && pEditGround->CheckEffect() == 0;
        walkable = done && CheckEditToWalk(at) != 0;
    } else if (g_own.menu == 1) {
        // As EdInitModeFinish: the fourth frame keeps the still the menu draws over (her copy's, her screen's).
        // The still is kept after the fourth frame's drawing (CoopP2DrawEditorEnd), as retail keeps it after the frame
        // (EdInitModeFinish runs after the draw); kept before it, in the tick, it was the frame before: a black flash
        // and a jump as her menu opened (Jep's playtest, 2026-10-21). The menu is made on the next tick.
        ++g_own.menu_ticks;

        if (g_own.menu_ticks == 4) {
            g_own.still_wanted = true;
            g_own.still_kept = false;

            // DC_COOP_MUTATE=p2georama_early_still: kept in the tick, before her frame (as before; twin).
            if (Mutated("p2georama_early_still") && NetplayLocalPlayer() == 2) {
                if (CTexture *still = TexManager.GetTexture("frame_image", -1)) {
                    EdSaveFrameImage(*still);
                    std::printf("coop-georama: her menu's still kept in the tick (early)\n");
                }
            }
        } else if (g_own.menu_ticks > 4) {
            static int blocks[6] = {16, 17, 18, 25, 32, 33};

            g_own.still_wanted = false;

            // (coop-stage4: made on her copy alone when local; the other's waits for the close command)
            if (!PartsMenuLocal() || PartsMenuHere()) {
                GamePad.MenuModeOn(120);
                EditMenuInit(blocks, EditMenuStatus.mode == EDIT_MENU_MODE_PLACE);
            }
            g_own.menu = 2;
            g_own.menu_sent = false;
            g_own.menu_opens++;

            // DC_COOP_P2GEORAMA_TEST_MENU_DUSK=1: the clock to just before dusk as her menu opens, so player 1's town
            // plays the time of day's event while it is open (both copies alike).
            if (TestEnvInt("DC_COOP_P2GEORAMA_TEST_MENU_DUSK", 0) != 0) {
                NowTime = 2.4999f;
                SaveData->SetNowTime(2.4999f);
            change_time_event = 1; // the clock's own way to the event (0x84), whatever hour the clock was at
                std::printf("coop-georama: test: the clock to 2.4999 as her menu opens\n");
            }
        }
    } else if (PartsMenuLocal()) {
        // coop-stage4: her copy alone steps and draws it (PartsMenuLocal); its close is the command, shut on every copy on its tick.
        if (PartsMenuHere() && !g_own.menu_sent) {
            int shut = 0;
            {
                WriteGuardMenu guard("her parts menu", true, &GamePad); // coop-guard (a test's: DC_QA_WRITE_GUARD; GamePad is her own pad here, g_own.pad)
                DrawRng        rng; // its random numbers her copy's own

                FullMenuStepping(kOwnPlayer); // (coop-p1parts: its Save page's writes as pokes, as his)
                FullMenuWritesBegin(true);
                shut = EditMenuLoop();
                FullMenuWritesEnd();
                FullMenuStepping(0);
            }

            if (shut != 0) {
                NetCommand command;
                command.kind = kCmdGeoMenu;
                command.a = (std::uint8_t) EditMenuStatus.mode;
                command.b = (std::uint16_t) (EditMenuStatus.parts + 1);
                command.c = (std::uint32_t) EditMenuStatus.event_no;
                NetplayQueueCommand(command);
                g_own.menu_sent = true;
                std::printf("coop-georama: her local parts menu shut on her copy (mode %d, part %d, event %d): its close command sent\n",
                            EditMenuStatus.mode, EditMenuStatus.parts, EditMenuStatus.event_no);
            } else if (g_own.menu_ticks++ == 5) {
                std::printf("coop-georama: her local parts menu steps on her copy alone\n");
            }
        }
    } else {
        // The menu steps and draws in one call; player 1's copy draws it where nobody sees it.
        gfx::TextureHandle previous = gfx::kNullTexture;

        if (NetplayLocalPlayer() != 2) {
            if (g_hidden == gfx::kNullTexture) {
                g_hidden = gfx::CreateRenderTarget(640, 480, true);
            }

            previous = gfx::CurrentRenderTarget();
            gfx::SetRenderTarget(g_hidden);
        }

        int shut = EditMenuLoop();

        if (NetplayLocalPlayer() != 2) {
            gfx::SetRenderTarget(previous);
        }

        if (shut != 0) {
            g_own.menu = 0;
            NowSelectParts = EditMenuStatus.mode == EDIT_MENU_MODE_PLACE ? EditMenuStatus.parts : -1;
            GamePad.AutoRepeatOff();
            GamePad.MenuModeOff();

            if (EditMenuStatus.mode == EDIT_MENU_MODE_EVENT) {
                Queue(EditMenuStatus.event_no + 0xC8, 0, "her menu");
            } else {
                std::printf("coop-georama: player 2's parts menu shuts, mode %d, part %d\n", EditMenuStatus.mode, NowSelectParts);
            }
        }
    }

    if ((pressed & kInputSelect) != 0 && !walkable) {
        Log(done ? "stays open (no ground under the cursor)" : "stays open (a part is still settling)");

        if (done && CoopDebug()) {
            float *c = ECursorFrame->position;

            std::printf("coop-georama: cursor y %.1f alt %.1f area %d (at y+500: alt %.1f area %d)\n", c[1], pEditGround->GetAlt(c[0], c[1], c[2]),
                        pEditGround->GetAreaCode(c[0], c[1], c[2]), pEditGround->GetAlt(c[0], c[1] + 500.0f, c[2]),
                        pEditGround->GetAreaCode(c[0], c[1] + 500.0f, c[2]));

            for (int i = 0; i < 4 && pEditGround->areas[i] != nullptr; i++) {
                CEditArea *a = pEditGround->areas[i];

                std::printf("coop-georama: area %d x %.0f..%.0f z %.0f..%.0f\n", i, a->offset_x, a->offset_x + a->unit_size * a->width, a->offset_z,
                            a->offset_z + a->unit_size * a->height);
            }
        }
    }

    Unreserve();
    SwapOut(); // player 1's editor back
    g_own.pad = GamePad;
    GamePad = town_pad;
    GameMode = town_mode;
    NowCamera = town_camera;
    g_own.before = pad;

    // The town's culling back as its own mode has it (the town's frame culls again before it draws).
    if (!TownBuilds()) {
        pEditGround->EditAreaClip(NowCamera, move_count > 0 ? -1.0f : 20000.0f);
    }

    // DC_COOP_P2GEORAMA_TEST_SYSMES=N: on her Georama's 400th tick (after the test's cursor move: a message holds the cursor), system message N (as an item's notice is set),
    // both copies alike, for the test that player 1's copy still shows it beside her hidden help bar.
    static const int test_mes = TestEnvInt("DC_COOP_P2GEORAMA_TEST_SYSMES", 0);

    if (test_mes > 0 && g_own.ticks == 400) {
        SetSystemMes(test_mes, 240, MES_POS_CENTRE, 0, NULL, NULL);
        std::printf("coop-georama: test: system message %d\n", test_mes);
    }

    // DC_COOP_DEBUG: where her cursor is, once a second.
    static const bool debug = CoopDebug();

    if (debug && g_own.ticks % 60 == 0) {
        Log("builds");
    }

    if (walkable) {
        coop::TownPlacePartner(at);

        // coop-pt1109c: as retail's way out of build mode, for those a new part blocks (CoopGeoramaLeaveVillagers), round Toan where he is.
        {
            sceVu0FVECTOR toan = {0.0f, 0.0f, 0.0f, 1.0f};

            Chara->GetPosition(toan);
            CoopGeoramaLeaveVillagers(&toan);
        }

        QueueLeaveEvents();
        End("closes (Select)");
    }
}

// coop-stage4 4d (Jep, 2026-11-07: Georama holds no merge, no exception): a merge comes at once whoever builds. His as the stayer goes on as
// it is (his editor is the game's globals, in the state set; linked, both copies step it). Any other builder's editor is its copy's alone
// apart: hers as the stayer has no way into the set (it would be a state-set change), and an arriver's is overwritten by it. So it parks
// as the link starts, as retail's Select leaves it, and opens again on its own Select once the copy has landed, from where the player
// stands: every copy then steps it as any Georama of the linked town. DC_COOP_MUTATE=merge_georama_holds (twin): the merge waits, as before.
static bool      g_parked = false;
static long long g_press_until = -1;

bool CoopGeoramaCarries() {
    return coop::Enabled() && !Mutated("merge_georama_holds") && mode == kTownMode && EdInteriorFlag == 0 && TownBuilds();
}

void CoopGeoramaPark(int player) {
    if (!CoopGeoramaCarries()) {
        return;
    }
    std::printf("coop-georama: player %d's Georama parks for the merge (town mode %d)\n", player, (int) GameMode);
    if (GameMode == ED_MODE_GEORAMA_MENU_INIT || GameMode == ED_MODE_GEORAMA_MENU) {
        EdExitMenu();
    }
    if (GameMode == ED_MODE_RETURN_MENU_GEORAMA) {
        PlayTimeCountFlag(1);
    }
    GamePad.AutoRepeatOff();
    NowSelectParts = -1;
    NowFocusParts = NULL;
    OldFocusParts = NULL;
    EditMenuStatus.mode = EDIT_MENU_MODE_CLOSED;
    EditMenuStatus.parts = -1;
    GameMode = ED_MODE_WALK;
    EdInitVillagerOnOff(EdVillager, EdVillagerInfo, pEditGround);
    g_parked = true;
}

bool CoopGeoramaParkedPress(int player) {
    const long long tick = NetplayLinkTick();

    if (g_parked && NetplayLinkUp() && !CoopWorldsApart() && !NetplayCatchUnderWay() && mode == kTownMode && EdInteriorFlag == 0 && GameMode == ED_MODE_WALK &&
        coop::TownPartnerShown(nullptr, nullptr) && change_time_event == 0 && !coop::PlayerInMenu(player)) {
        g_parked = false;
        g_press_until = tick + 4;
        std::printf("coop-georama: player %d's parked Georama opens again by its Select at link tick %lld, landed\n", player, tick);
    }
    return tick < g_press_until;
}

// coop-stage4 r4 (Jep, 2026-11-08, answer 2: the state-set change approved): her Georama as the stayer stays exactly where it is at a
// merge. Apart her editor is her copy's globals (retail's Georama); linked it is her own (g_own, stepped on both copies). As the link
// starts on her copy (the sender) her retail editor becomes her own, its cursor, camera and part in hand as they are (her parts menu
// shuts, as her Select-out would leave it), the town's globals go back to walking (as CoopGeoramaPark leaves them), and her own
// travels with the state set (an extra, CoopGeoramaSyncBytes), so the taker steps the same editor once they are together.
// DC_COOP_MUTATE=georama_stayer_parks (twin): it parks and opens again by her Select, as 4d left it.
void CoopGeoramaHerToOwn() {
    if (!CoopGeoramaCarries() || !CoopWorldsApart()) {
        return;
    }
    if (Mutated("georama_stayer_parks")) {
        CoopGeoramaPark(2);
        return;
    }
    const int was = GameMode;

    if (was == ED_MODE_GEORAMA_MENU_INIT || was == ED_MODE_GEORAMA_MENU) {
        EdExitMenu();
    }
    if (was == ED_MODE_RETURN_MENU_GEORAMA) {
        PlayTimeCountFlag(1);
    }
    SaveEditor(g_own.editor);
    g_own.pad = GamePad;
    g_own.pad.AutoRepeatOff();
    g_own.pad.MenuModeOff();
    g_own.swapped = false;
    g_own.menu = 0;
    g_own.menu_ticks = 0;
    g_own.paused = false;
    g_own.on = true;
    g_own.map = MapNo;
    g_own.ticks = 0;
    g_own.prev = 0xFFFF; // (a button held as the link starts is no press)
    g_own.before = InputPadState{};
    g_own.merge_hold = true;
    std::printf("coop-georama: player 2's Georama goes on through the merge as her own: cursor (%.0f %.0f), part in hand %d (town mode %d)\n",
                g_own.editor.frame_pos[0], g_own.editor.frame_pos[2], g_own.editor.select_parts, was);
    GamePad.AutoRepeatOff();
    NowSelectParts = -1;
    NowFocusParts = NULL;
    OldFocusParts = NULL;
    EditMenuStatus.mode = EDIT_MENU_MODE_CLOSED;
    EditMenuStatus.parts = -1;
    GameMode = ED_MODE_WALK;
    EdInitVillagerOnOff(EdVillager, EdVillagerInfo, pEditGround);
}

// Test hook (DC_WORLDS_TEST_MERGE_IN_MENU_P2=buildpart, her copy apart): once in retail's Georama, the first part of the map's catalogue
// in stock and in her hand, as her parts menu hands it back, for the check that it is still in her hand after the merge.
void CoopGeoramaTestPickApart() {
    static bool done = false;

    if (done || GameMode != ED_MODE_GEORAMA || mode != kTownMode || EdInteriorFlag != 0) {
        return;
    }
    for (int i = 0; i < EditPartsInfo.parts_max && i < 24; i++) {
        EDITPARTS_INFO *info = EditPartsInfo.GetPartsInfo(i);

        if (info != nullptr && info->header != nullptr && info->width > 0) {
            info->obtained = 1;
            info->stock = info->placed + 1;
            EditMenuStatus.mode = EDIT_MENU_MODE_PLACE;
            EditMenuStatus.parts = i;
            NowSelectParts = i;
            done = true;
            std::printf("coop-georama: test: part %d in her hand apart\n", i);
            return;
        }
    }
}

// Her own Georama with the state set: taken on the sender, put back on the taker (her copy's drawing marks stay each copy's).
// Cleared before every apply, so a set without it (an older fixture) leaves the taker's own as it is.
namespace {
constexpr std::uint32_t kOwnSyncMagic = 0x47454F31u; // 'GEO1'
struct OwnSync {
    std::uint32_t magic = 0;
    Own           own;
};
OwnSync g_own_sync;
} // namespace

unsigned char *CoopGeoramaSyncBytes(std::size_t *size) {
    *size = sizeof g_own_sync;
    return reinterpret_cast<unsigned char *>(&g_own_sync);
}

void CoopGeoramaSyncTake() {
    g_own_sync.own = g_own;
    g_own_sync.own.swapped = false;
    g_own_sync.magic = kOwnSyncMagic;
}

void CoopGeoramaSyncClear() {
    g_own_sync.magic = 0;
}

void CoopGeoramaSyncApply() {
    if (g_own_sync.magic != kOwnSyncMagic) {
        return;
    }
    g_own_sync.magic = 0;
    const bool still_wanted = g_own.still_wanted, still_kept = g_own.still_kept;

    if (g_own_sync.own.on != g_own.on || g_own_sync.own.on) {
        std::printf("coop-georama: her own Georama from the state set: open %d (here %d), hold %d, cursor (%.0f %.0f), part in hand %d\n", g_own_sync.own.on ? 1 : 0,
                    g_own.on ? 1 : 0, g_own_sync.own.merge_hold ? 1 : 0, g_own_sync.own.editor.frame_pos[0], g_own_sync.own.editor.frame_pos[2],
                    g_own_sync.own.editor.select_parts);
    }
    g_own = g_own_sync.own;
    g_own.swapped = false;
    g_own.still_wanted = still_wanted;
    g_own.still_kept = still_kept;
}

void CoopP2GeoramaResetForSession() {
    g_own = Own{};
    g_help_left = 0;
    p1_ticks = 0;
    p1_done = false;
}
