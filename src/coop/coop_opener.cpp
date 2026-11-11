#include "coop/game_ns.hpp"
#include "coop/coop_opener.hpp"

#include "coop/coop.hpp"
#include "coop/coop_actor.hpp"
#include "coop/coop_camera.hpp"
#include "coop/coop_core.hpp"
#include "coop/coop_menu.hpp"
#include "coop/coop_message.hpp"
#include "coop/coop_present.hpp"
#include "coop/netplay.hpp"
#include "coop/netplay_cmd.hpp"

#include <libvu0.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

#include "btactstatus.hpp"
#include "btitem.hpp"
#include "btmisc.hpp" // getAtraToSaveData
#include "coop/coop_interact.hpp" // the Atla's stance for a member who is not Toan (PlayerAtla*); the partner's body (PartnerPresent*)
#include "coop/coop_inventory.hpp" // the partner's own bag (PartnerCheckItemGet, PartnerGetItem)
#include "savedata.hpp"
#include "camerafollow.hpp"
#include "character.hpp"
#include "dataread.hpp"
#include "dngmessageman.hpp"
#include "dngstatusdata.hpp"
#include "dun/gameloop.hpp"
#include "dungeonmap.hpp"
#include "gamepad.hpp"
#include "itemdata.hpp"
#include "menu_save.hpp" // PlayerAllItemCheck
#include "platform/arena.hpp"
#include "platform/input.hpp"
#include "snd.hpp"
#include "sysmes.hpp"
#include "userstatus.hpp"

namespace dc::game {
extern s32            iventActive;
extern s32            gameTask;
extern int            iventInfo;
extern int            CMonUnitHold;
extern int            CMonUnitHyde;
extern int            CEffectHold;
extern int            CEffectHyde;
extern s32            driveNoInterpolate;
extern s32            selectMapNo;
extern s32            itemOpenSmallFlag;
extern s32            itemOpenBigFlag;
extern CCameraFollow  MainCamera__4;
extern CCameraFollow *NowCamera__3;

int  createAttachVolume(int item_no, int dungeon);
void BtGetTreasureboxSmall_Init(int dungeon);
int  BtGetTreasureboxSmall_Loop();
void BtGetTreasureboxBig_Init();
int  BtGetTreasureboxBig_Loop();
void BtAtraGetShort_Init();
int  BtAtraGetShort_Loop(int dungeon, int floor);
extern s32            atraGetStatus;
extern s32            iventMarker;
extern s32            atraGetMsgBord;
extern s32            infoMap;
extern s32            infoMapOld;
} // namespace dc::game

namespace coop {

namespace {

// repin: the Atla's pose pack as upstream's BtAtraGetShort_Init names it (retail's BtAtraShortCharaFile, a global the old pin had).
char kAtlaPoseFile[] = "dun/mainchara/c01d_ex00.chr";

// The world half, per player (1 up = index + 1): in the state set (snapshot-allow.txt coop::*::g_present) and the checksum
// (PresentHash), the same on every copy. refusal: a small chest's CheckItemGet (1 no room, 2 other), a large one's 1 (its
// owner has ten); the box then stays shut and nobody is posed.
struct Present {
    std::uint8_t  kind = kPresentNone;
    std::int8_t   refusal = 0;
    std::int16_t  active = -1; // the floor's event slot
    std::int16_t  entry = -1;  // the box
    std::int32_t  item = -1;
    std::int32_t  volume = 0;
    std::int32_t  ticks = 0; // since the start, every copy alike
    float         turn = 0.0f; // an Atla: the effect's turn to a member who stands in its stance (posed)
    std::int8_t   posed = 0;
    std::int8_t   named = 0;
    std::int16_t  pad = 0;
};

Present g_present[kMaxPlayers];

// The opener's chest pose pack (its member's _ex00.chr, retail's Init loads it into BtCashBuffer for sled 0's LoadPackData),
// loaded on every copy into pinned arenas: one address on every copy, carried by the state set, as CharaMain's motions point
// into it.
constexpr std::size_t kPosePackBytes = 512u * 1024;
constexpr int         kPoseQuads = 0x10000;
const char            kPosePackArena = 0;
const char            kPoseModelArena = 0;
CDataAlloc2<1>        g_pose_arena;

const char *const kChestPacks[6] = {"dun/mainchara/c01d_ex00.chr", "dun/mainchara/c04b_ex00.chr", "dun/mainchara/c06b_ex00.chr",
                                    "dun/mainchara/c05a_ex00.chr", "dun/mainchara/c10b_ex00.chr", "dun/mainchara/c18a_ex00.chr"};

// The local half on this copy (never in the state set: the name stays out of snapshot-allow.txt's patterns).
struct Engine {
    int            player = 0; // the presentation it runs, 0 none
    int            kind = kPresentNone;
    bool           started = false;
    bool           done = false;
    CCameraFollow *camera = nullptr;
    CGamePad       pad{};
    InputPadState  before{};
    int            steps = 0;
};

Engine g_opener_engine;
constexpr int kBoxSoundTick = 10;
constexpr int kAtlaNameTick = 130;
bool   g_in_engine = false;
bool   g_installed = false;
int    g_reload_due = 0; // the reload test hook's word (PresentTestStateDue)
float  g_lit[kMaxPlayers] = {}; // the Atla light's rate each player's body was last drawn with on this copy (the test's line)

// The engine's screen state, as a state set's take or apply on its copy keeps it (PresentSetScope).
struct ScreenKept {
    s32  small = 0, big = 0, atla = 0, board = 0, map = 0;
    bool window = true;
};
ScreenKept g_screen_kept;

void ScreenSave(ScreenKept &k) {
    k.small = itemOpenSmallFlag;
    k.big = itemOpenBigFlag;
    k.atla = atraGetStatus;
    k.board = atraGetMsgBord;
    k.map = infoMap;
    k.window = DngMessMan.enabled;
}

void ScreenPut(const ScreenKept &k) {
    itemOpenSmallFlag = k.small;
    itemOpenBigFlag = k.big;
    atraGetStatus = k.atla;
    atraGetMsgBord = k.board;
    infoMap = k.map;
    DngMessMan.enabled = k.window;
}

bool LoadPosePack(const char *path) {
    static u_char *pack = nullptr;
    static u_char *model = nullptr;

    if (path == nullptr) {
        return false;
    }
    if (pack == nullptr) {
        pack = ArenaBlock(&kPosePackArena, kPosePackBytes);
        model = ArenaBlock(&kPoseModelArena, static_cast<std::size_t>(kPoseQuads) * 16);
    }

    int size = 0;

    if (LoadFile((char *) path, reinterpret_cast<u_int *>(pack), &size) == 0 || size <= 0 || size > (int) kPosePackBytes) {
        std::fprintf(stderr, "coop-opener: cannot load %s\n", path);
        return false;
    }
    g_pose_arena.base = model;
    g_pose_arena.buffer = model;
    g_pose_arena.used = 0;
    g_pose_arena.limit = kPoseQuads;
    CharaMain.LoadPackData(reinterpret_cast<u_int *>(pack), (char *) "base2.cfg", &g_pose_arena, &g_pose_arena);
    return true;
}

// The opening pose, every copy: retail's sled 1 (the body on the box, unturned, the opening motion), then the hold motion at
// the pose's frame 79 (retail's PAL rule, read from the body: the copies step it alike).
void PoseBody(const Present &p) {
    sceVu0FVECTOR position;

    sceVu0CopyVector(position, NowDngMap->boxes[p.entry].pos);
    CharaMain.SetPosition(position);
    CharaMain.SetRotation(0.0f, 0.0f, 0.0f);
    BtActStatus.motion_no = p.kind == kPresentBig ? 0x2C : 0x2A;
}

void EndWorld(int player, std::int64_t tick) {
    Present &p = g_present[player - 1];

    if (p.kind == kPresentNone) {
        return;
    }
    if (player == 1) { // the game's player (players past 2 get theirs with their bodies)
        if (p.kind == kPresentAtla) {
            // retail's step 5: turned round (hers to the ball, Toan's -PI), the game's camera following again
            CharaMain.SetRotation(0.0f, PlayerAtlaEndTurn(), 0.0f);
            MainCamera__4.SetSpeed(6.0f);
            MainCamera__4.FollowOn();
        } else if (p.refusal != 0) {
            NowDngMap->boxes[p.entry].lid_angle = 0.0f;
        } else {
            sceVu0FVECTOR position; // retail's close: ten in front of the box, turned round to it

            sceVu0CopyVector(position, CharaMain.pos);
            position[2] += 10.0f;
            CharaMain.SetPosition(position);
            CharaMain.SetRotation(0.0f, -PI, 0.0f);
        }
        BtActStatus.motion_no = 0;
        BtActStatus.shadow_visible = true;
        BtActStatus.in_presentation = 0;
        UserStatus->step_disable = 0;
    } else { // the partner's body (players past 2 have no body of their own yet: PresentStartFor never starts theirs)
        PartnerPresentEnd(p.kind, p.posed != 0);
    }
    std::fprintf(stderr, "coop-opener: player %d's %s ends at tick %lld after %d ticks\n", player,
                 p.kind == kPresentAtla ? "Atla" : p.kind == kPresentBig ? "large chest" : "small chest",
                 (long long) tick, (int) p.ticks);
    p = Present{};
}

// kCmdPresent: a = the kind that ended, b = the player (1 up). Linked, a copy speaks for its own player alone.
void ApplyEnd(int sender, const NetCommand &command, std::int64_t tick) {
    const int player = command.b;

    if (player < 1 || player > kMaxPlayers || (NetplayLinkUp() && player != sender)) {
        std::fprintf(stderr, "coop-opener: end for player %d from player %d refused at tick %lld\n", player, sender, (long long) tick);
        return;
    }
    if (g_present[player - 1].kind != command.a) {
        std::fprintf(stderr, "coop-opener: end of kind %d for player %d, who has %d, at tick %lld (ignored)\n", (int) command.a, player,
                     (int) g_present[player - 1].kind, (long long) tick);
        return;
    }
    EndWorld(player, tick);
}

// What the engine's step may write of the world, kept and put back after it (the recipe keeps the rest off: the item, the body).
struct WorldKept {
    explicit WorldKept(const Present &p) : p_(p) {
        std::memcpy(camera_, (const void *) &MainCamera__4, sizeof camera_);
        now_camera_ = NowCamera__3;
        holds_[0] = CMonUnitHold;
        holds_[1] = CMonUnitHyde;
        holds_[2] = CEffectHold;
        holds_[3] = CEffectHyde;
        info_ = iventInfo;
        active_ = iventActive;
        std::memcpy(act_, (const void *) &BtActStatus, sizeof act_);
        step_disable_ = UserStatus->step_disable;
        if (p.kind == kPresentAtla) {
            std::memcpy(atla_, (const void *) &NowDngMap->atra[p.entry], sizeof atla_);
        } else {
            std::memcpy(box_, (const void *) &NowDngMap->boxes[p.entry], sizeof box_);
        }
        std::memcpy(event_, (const void *) &NowDngMap->events[p.active], sizeof event_);
        interpolate_ = driveNoInterpolate;
    }
    ~WorldKept() {
        std::memcpy((void *) &MainCamera__4, camera_, sizeof camera_);
        NowCamera__3 = now_camera_;
        CMonUnitHold = holds_[0];
        CMonUnitHyde = holds_[1];
        CEffectHold = holds_[2];
        CEffectHyde = holds_[3];
        iventInfo = info_;
        iventActive = active_;
        std::memcpy((void *) &BtActStatus, act_, sizeof act_);
        UserStatus->step_disable = step_disable_;
        if (p_.kind == kPresentAtla) {
            std::memcpy((void *) &NowDngMap->atra[p_.entry], atla_, sizeof atla_);
        } else {
            std::memcpy((void *) &NowDngMap->boxes[p_.entry], box_, sizeof box_);
        }
        std::memcpy((void *) &NowDngMap->events[p_.active], event_, sizeof event_);
        driveNoInterpolate = interpolate_;
    }
    WorldKept(const WorldKept &) = delete;
    WorldKept &operator=(const WorldKept &) = delete;

  private:
    const Present &p_;
    alignas(16) unsigned char camera_[sizeof(CCameraFollow)];
    CCameraFollow *now_camera_;
    int            holds_[4];
    int            info_;
    int            active_;
    alignas(16) unsigned char act_[sizeof BtActStatus];
    int           step_disable_;
    alignas(16) unsigned char box_[sizeof NowDngMap->boxes[0]];
    alignas(16) unsigned char atla_[sizeof NowDngMap->atra[0]];
    alignas(16) unsigned char event_[sizeof NowDngMap->events[0]];
    int           interpolate_;
};

void PadIn(Engine &e, int player) {
    InputPadState now{};

    NetplayPlayerPad(player, now);

    PAD_DATA *data = reinterpret_cast<PAD_DATA *>(&e.pad);

    for (int i : {0, 2}) {
        const InputPadState &from = i == 0 ? now : e.before;
        PAD_STATUS          &to = data[i].input.status;

        to.button = from.buttons;
        to.left_x = from.left_x;
        to.left_y = from.left_y;
        to.right_x = from.right_x;
        to.right_y = from.right_y;
    }
    e.before = now;
}

// The engine's presentation state off this copy's screen (its end, or a state load that ended the world half under it).
void EngineClear(const char *why) {
    Engine &e = g_opener_engine;

    if (e.player == 0) {
        return;
    }
    if (!e.done) {
        itemOpenSmallFlag = 0;
        itemOpenBigFlag = 0;
        DngMessMan.enabled = true;
        SetMIniMapStatus(1);
        ClearSystemMes();
    }
    std::fprintf(stderr, "coop-opener: player %d's engine stops (%s) after %d steps\n", e.player, why, e.steps);
    e = Engine{};
}

void EngineTick(int player) {
    Present &p = g_present[player - 1];
    Engine  &e = g_opener_engine;

    if (e.player != 0 && (e.player != player || e.kind != p.kind)) {
        EngineClear("the world half ended");
    }
    if (e.player == 0) {
        e.player = player;
        e.kind = p.kind;
        e.pad = GamePad;
        NetplayPlayerPad(player, e.before);
    }
    if (e.done) {
        return; // its end sent; the world frees the opener on the command's tick
    }

    bool done = false;
    {
        present::Scope scope(present::For{player}, COOP_PRESENT_HERE); // the guard: the step leaves the lockstep's state as it was
        DrawRng        rng;
        WorldKept      kept(p);
        CCameraFollow *world_camera = NowCamera__3;

        PadIn(e, player);
        std::swap(GamePad, e.pad);
        if (e.camera != nullptr) {
            NowCamera__3 = e.camera;
        }
        iventActive = p.active;
        g_in_engine = true;
        if (!e.started) {
            if (p.kind == kPresentAtla) {
                BtAtraGetShort_Init();
            } else if (p.kind == kPresentBig) {
                BtGetTreasureboxBig_Init();
            } else {
                BtGetTreasureboxSmall_Init(selectMapNo);
            }
            e.started = true;
        } else {
            done = p.kind == kPresentAtla ? BtAtraGetShort_Loop(selectMapNo, UserStatus->cur_floor) == 1
                                          : (p.kind == kPresentBig ? BtGetTreasureboxBig_Loop() : BtGetTreasureboxSmall_Loop()) != 0;
        }
        g_in_engine = false;
        e.camera = NowCamera__3 != world_camera && NowCamera__3 != &MainCamera__4 ? NowCamera__3 : nullptr;
        std::swap(GamePad, e.pad);
        (void) world_camera;
    }
    e.steps++;

    if (done) {
        e.done = true;
        e.camera = nullptr;

        NetCommand command;

        command.kind = kCmdPresent;
        command.a = p.kind;
        command.b = (std::uint16_t) player;
        NetplayQueueCommand(command);
        std::fprintf(stderr, "coop-opener: player %d's engine done after %d steps, end sent at tick %lld\n", player, e.steps,
                     (long long) NetplayLinkTick());
    }
}

} // namespace

// coop-soakfix3 (Jep: a join never waits): a co-op session's host alone too, not only once linked. Alone, his chest or Atla ran
// retail's presentation task (BOX_*_LOOP, ATRA_GET_LOOP) until his Cross on its message, which a join's take waits out (3
// minutes, then the replay from tick 0); on the local path the world stays at play, the set carries present[n] and the joiner
// lands at once beside his posed body (opt7-design.md, "host alone"). Alone the path is the same: his engine's end command
// applies at once. DC_COOP_MUTATE=present_alone_retail (twin): linked only, as before.
bool PresentLocalOn() {
    const bool session = NetplayPartnerLinked() || (NetplayConfigured() && !Mutated("present_alone_retail"));

    return Enabled() && MenusOn() && session && !Mutated("present_lockstep");
}

void PresentInstall() {
    if (!g_installed) {
        NetplaySetCommandHandler(kCmdPresent, ApplyEnd);
        g_installed = true;
    }
}

// An Atla's world half (retail's Init and step 0, their world part), every copy at the agreed Cross: the ball taken (its save
// bit), the opener posed (Toan's reach, or a member's stance where she pressed, coop-atlaswap), still and out of harm.
static bool StartAtla(int player, int active) {
    CDngStatusData *status = (CDngStatusData *) UserStatus;
    const int       index = NowDngMap->events[active].index;
    Present        &p = g_present[player - 1];
    sceVu0FVECTOR   stood;
    sceVu0FVECTOR   ball;

    p = Present{};
    p.kind = kPresentAtla;
    p.active = (std::int16_t) active;
    p.entry = (std::int16_t) index;
    if (player != ActorNumber(0)) { // the partner's body: her stance (coop-actorswap's rules), the effect turned to her
        p.item = status->atra_registry[selectMapNo][NowDngMap->atra[index].atra_no].id;
        NowDngMap->atra[index].used = 0;
        NowDngMap->events[active].kind = DNG_EVENT_NONE;
        getAtraToSaveData(p.item, NowDngMap->atra[index].atra_no, SaveData, selectMapNo, UserStatus->cur_floor);
        float turn = 0.0f;

        PartnerPresentAtla(index, NetplayLocalPlayer() != player, &turn);
        p.turn = turn;
        p.posed = 1;
        std::fprintf(stderr, "coop-opener: player %d takes Atla %d (ball %d) at tick %lld: posed %d\n", player, p.item, index,
                     (long long) NetplayLinkTick(), (int) p.posed);
        return true;
    }
    ResetMovePower();
    UserStatus->step_disable = 1;
    BtActStatus.in_presentation = 1;
    BtActStatus.motion_no = 0;
    iventMarker = 0;
    iventInfo = DNG_EVENT_NONE;
    sceVu0CopyVector(stood, CharaMain.pos);
    sceVu0CopyVector(ball, NowDngMap->atra[index].pos);
    p.item = status->atra_registry[selectMapNo][NowDngMap->atra[index].atra_no].id;
    NowDngMap->atra[index].used = 0;
    NowDngMap->events[active].kind = DNG_EVENT_NONE;
    getAtraToSaveData(p.item, NowDngMap->atra[index].atra_no, SaveData, selectMapNo, UserStatus->cur_floor);

    // The pose pack, as retail's Init and step 0 load it (a member who is not Toan: her own chest set, coop-atlaswap).
    const char *pack = PlayerAtlaPack(kAtlaPoseFile);
    const bool  toan = UserStatus->cur_chara == CHARA_TOAN;

    if ((toan || PlayerAtlaPosed()) && PlayerAtlaPackFits()) {
        LoadPosePack(pack);
    }
    BtActStatus.shadow_visible = false;
    CharaMain.SetPosition(ball);
    CharaMain.SetRotation(0.0f, 0.0f, 0.0f);
    BtActStatus.motion_no = toan ? 0x2E : 0;
    if (float turn = 0.0f; PlayerAtlaStance(ball, stood, &turn)) {
        p.turn = turn;
        p.posed = 1;
    }
    driveNoInterpolate = 1;
    std::fprintf(stderr, "coop-opener: player %d takes Atla %d (ball %d) at tick %lld: posed %d\n", player, p.item, index,
                 (long long) NetplayLinkTick(), (int) p.posed);
    return true;
}

bool PresentStartFor(int player, int active) {
    // Any player of the table; a body to pose is the resident's (player 1) or the partner's (player 2): players past 2 have no
    // body in the world yet, and theirs starts with it.
    if (player < 1 || player > 2 || !PresentLocalOn() || active < 0 || g_present[player - 1].kind != kPresentNone) {
        return false;
    }

    PresentInstall();
    if (NowDngMap->events[active].kind == DNG_EVENT_ATRA) {
        return StartAtla(player, active);
    }

    CDngStatusData *status = (CDngStatusData *) UserStatus;
    const int       entry = NowDngMap->events[active].index;
    TREASURE_BOX   &box = NowDngMap->boxes[entry];
    const bool      big = box.kind == TREASURE_BOX_LARGE;
    Present        &p = g_present[player - 1];
    const bool      resident = player == ActorNumber(0); // the game's player's body (CharaMain), else the partner's

    p = Present{};
    p.kind = big ? kPresentBig : kPresentSmall;
    p.active = (std::int16_t) active;
    p.entry = (std::int16_t) entry;
    p.item = box.item_no;
    if (resident) {
        ResetMovePower();
    }

    // Retail's Init, its world part (the refusal, the item and its volume: createAttachVolume draws the game's random, so
    // here, on every copy).
    if (big) {
        p.refusal = status->CheckWeaponRot(p.item) >= 10 ? 1 : 0;
        if (p.refusal == 0 && p.item == ITEM_WEAPON_STEVE && PlayerAllItemCheck(ITEM_WEAPON_STEVE) != 0) {
            p.item = ITEM_WEAPON_BONE_SLINGSHOT;
        }
    } else {
        p.refusal = (std::int8_t) (resident ? status->CheckItemGet(p.item) : PartnerCheckItemGet(p.item)); // (coop-inventory: her own bag)
        if (p.refusal == 0) {
            if (p.item >= ITEM_ATTACH_START && ITEM_NAME_TBL_NEW[p.item - ITEM_ATTACH_START] == NULL) {
                p.item = ITEM_ATTACH_PERIDOT;
            }
            p.volume = createAttachVolume(p.item, selectMapNo);
        }
    }

    if (resident) {
        iventInfo = DNG_EVENT_NONE;
        UserStatus->step_disable = 1;
        BtActStatus.in_presentation = 1;
        BtActStatus.motion_no = 0;
    }

    if (p.refusal != 0) {
        box.lid_angle = -20.0f;
    } else {
        // Jep (11-09, opt7): the item goes in at the opening press, on every copy at once.
        NowDngMap->events[active].kind = DNG_EVENT_NONE;
        box.lid_angle = -30.0f;
        box.closed = 0;
        if (!big && (unsigned int) (p.item - ITEM_MAP) < 2U) {
            if (p.item == ITEM_MAP) {
                BtEquipMap = 1;
            } else {
                BtEquipMasuisyou = 1;
            }
        } else if (resident || big || !PartnerGetItem(p.item, -1, false)) { // (coop-inventory: a bag item into her own bag)
            status->GetItem(p.item, big ? 0 : p.volume);
        }
        if (!resident) {
            p.posed = 1;
        } else if (UserStatus->cur_chara >= 0 && UserStatus->cur_chara < 6 && LoadPosePack(kChestPacks[UserStatus->cur_chara])) {
            BtActStatus.shadow_visible = false;
            PoseBody(p);
        }
    }
    if (!resident) {
        PartnerPresentChest(entry, big, p.item, p.refusal == 0, NetplayLocalPlayer() != player);
    }
    std::fprintf(stderr, "coop-opener: player %d opens %s box %d at tick %lld: item %d x%d refusal %d\n", player, big ? "large" : "small", entry,
                 (long long) NetplayLinkTick(), p.item, p.volume, (int) p.refusal);
    return true;
}

bool PlayerPresenting(int player) {
    return player >= 1 && player <= kMaxPlayers && g_present[player - 1].kind != kPresentNone;
}

bool AnyPresenting() {
    for (const Present &p : g_present) {
        if (p.kind != kPresentNone) {
            return true;
        }
    }
    return false;
}

// One tick of a player's presentation, its world half on every copy alike, then on the opener's own copy its engine. resident:
// the game's player (CharaMain's body, DunMoveChara's play task), else the partner (her walk's StepBusy).
static bool StepWorld(int opener, bool resident) {
    const bool here = NetplayLocalPlayer() == opener; // this copy is the opener's: the engine runs here

    if (!PlayerPresenting(opener)) {
        if (g_opener_engine.player == opener) {
            EngineClear("the world half ended");
        }
        return false;
    }

    Present &p = g_present[opener - 1];

    p.ticks++;
    // (once each: the load puts the presentation's ticks back to 40, so 70 comes again; this copy's own flags, alike on both)
    if (static const bool reload = TestEnvOn("DC_COOP_TEST_PRESENT_RELOAD"); reload) {
        static bool taken = false;
        static bool loaded = false;

        if (p.ticks == 40 && !taken) {
            taken = true;
            g_reload_due = 1;
        } else if (p.ticks == 70 && taken && !loaded) {
            loaded = true;
            g_reload_due = 2;
        }
    }
    // The box's own sound on the others' copies, placed at the box (heard by distance, coop-chestsound's rule), about where the
    // opener's presentation plays it (retail: its effect's frame 19-20, some ten ticks in). The jingle is the opener's alone.
    // DC_COOP_AUDIO_MUTATE=chestflat (twin): flat, as retail's presentation played it on every copy; flat out of any place around
    // it too (coop-opt7e: a partner's step plays inside her own SoundFrom, which placed it at her and hid the twin).
    if (p.ticks == kBoxSoundTick && p.refusal == 0 && p.kind != kPresentAtla && !here) {
        const char *audio = TestEnv("DC_COOP_AUDIO_MUTATE");
        const int   se = p.kind == kPresentBig ? SE_CHEST_OPEN_BIG : SE_CHEST_OPEN_SMALL;

        if (audio != nullptr && std::strstr(audio, "chestflat") != nullptr) {
            SoundFrom flat(nullptr);
            SndSePlay(se, -1, 0);
        } else {
            present::Scope at(present::At{NowDngMap->boxes[p.entry].pos}, COOP_PRESENT_HERE);
            SndSePlay(se, -1, 0);
        }
    }

    bool held_up = false; // the opener holds the item up this tick (retail's frame 79)

    if (!resident) {
        held_up = PartnerPresentStep(p.kind, p.ticks, !here);
    } else if (p.kind == kPresentAtla) {
        // retail's step 1, its world part: Toan's reach held at its frame 129 (PAL's rule), a member's stance stepped
        if (BtActStatus.motion_no == 0x2E && CharaMain.GetNowTime() >= 129.0f && !PlayerAtlaPosed()) {
            BtActStatus.motion_no = 0x2F;
        }
        PlayerAtlaStep();
    } else {
        // The hold motion at the opening's frame 79 (retail's PAL rule, read from the body, which every copy steps alike).
        const int opening = p.kind == kPresentBig ? 0x2C : 0x2A;

        if (p.refusal == 0 && BtActStatus.motion_no == opening && CharaMain.GetNowTime() >= 79.0f) {
            BtActStatus.motion_no = opening + 1;
            held_up = true;
        }
    }
    if (p.kind == kPresentAtla) {
        // Its name on the others' screens, fading (coop-closetitem, Jep 10-31), about where the opener's shows it (the effect's
        // frame 129.5). DC_COOP_MUTATE=atlaname_opener_only (twin): never.
        if (p.ticks == kAtlaNameTick && !here && !Mutated("atlaname_opener_only")) {
            MessageBy by(opener);
            AtraGetMes(selectMapNo, p.item, 0x28);
            p.named = 1;
        }
    } else if (held_up && !here) {
        // Where the opener's screen shows the item's message (retail's frame 79): a key item's on the others' too, fading.
        OthersKeyItemMes(opener, p.item, p.volume);
    }
    if (here) {
        EngineTick(opener);
    }
    return true;
}

bool PresentPlayTick() {
    // The game's player (the actor table's resident entry: CharaMain's body), the one whose free play this task runs.
    return StepWorld(ActorNumber(0), true);
}

bool PresentStepFor(int player) {
    return player != ActorNumber(0) && player >= 1 && player <= kMaxPlayers && StepWorld(player, false);
}

int PresentKindOf(int player) {
    return player >= 1 && player <= kMaxPlayers ? g_present[player - 1].kind : kPresentNone;
}

bool PresentEngineStance(float *turn) {
    if (g_opener_engine.player <= 0) {
        return false;
    }
    const Present &p = g_present[g_opener_engine.player - 1];

    *turn = p.turn;
    return p.posed != 0;
}

bool PresentEngine() {
    return g_in_engine;
}

int PresentEngineItem() {
    return g_opener_engine.player > 0 ? g_present[g_opener_engine.player - 1].item : -1;
}

int PresentEngineVolume() {
    return g_opener_engine.player > 0 ? g_present[g_opener_engine.player - 1].volume : 0;
}

int PresentEngineRefusal() {
    return g_opener_engine.player > 0 ? g_present[g_opener_engine.player - 1].refusal : 0;
}

CCameraFollow *PresentEngineCamera() {
    return g_opener_engine.player > 0 && !g_opener_engine.done ? g_opener_engine.camera : nullptr;
}

int PresentLitPlayer() {
    if (atraGetStatus == 0) {
        return 0;
    }
    if (g_opener_engine.player > 0 && g_opener_engine.kind == kPresentAtla && !Mutated("atlalight_resident")) {
        return g_opener_engine.player; // the opener's own copy runs retail's Atla as its engine: the light is the opener's
    }
    return ActorNumber(0); // retail's own task (unlinked, or player 1's under present_lockstep): the game's player
}

bool PresentLight(int player, const float *stood, float light[4][4], float colour[4][4]) {
    if (player < 1 || player > kMaxPlayers) {
        return false;
    }
    g_lit[player - 1] = 0.0f;

    CFrame *lamp = PresentLitPlayer() == player ? shortAtraEffect.frame->SearchFrame((char *) "light01") : nullptr;

    if (lamp == nullptr) {
        return false;
    }

    // Retail's (Draw_MainUnit): a point light at the effect's "light01" frame on the body standing at stood.
    sceVu0FVECTOR up = {lamp->local[3][0], lamp->local[3][1], lamp->local[3][2], 1.0f};
    sceVu0FVECTOR world;
    sceVu0FVECTOR towards;

    lamp->GetWorldPosition(world, up);
    towards[0] = world[0] - stood[0];
    towards[1] = world[1] - stood[1] - 12.0f;
    towards[2] = world[2] - stood[2];
    towards[3] = 0.0f;

    float dist = sqrtf(towards[0] * towards[0] + towards[1] * towards[1] + towards[2] * towards[2]);

    if (dist <= 10.0f) {
        dist = 10.0f;
    }

    const float power = 5.0f * (100.0f / (dist * dist));

    sceVu0Normalize(towards, towards);
    light[0][3] = towards[0] * power;
    light[1][3] = towards[1] * power;
    light[2][3] = towards[2] * power;
    light[3][3] = 0.0f;
    colour[3][0] = 0.8359375f * atraGetStatusRate__2;
    colour[3][1] = 0.9765625f * atraGetStatusRate__2;
    colour[3][2] = 0.66015625f * atraGetStatusRate__2;
    colour[3][3] = 0.5f * atraGetStatusRate__2;
    g_lit[player - 1] = atraGetStatusRate__2 > 0.0f ? atraGetStatusRate__2 : 1.0f;
    return true;
}

void PresentTestTick(bool held) {
    static const int  opener = TestEnvInt("DC_COOP_TEST_OPENER", 0); // (1: player 1's; else the player under test)
    static long long  last = -1;
    const long long   clock = NetplayTestPressClock();

    if (opener < 1 || opener > kMaxPlayers || clock < 0 || clock % 10 != 0 || clock == last) {
        return;
    }
    last = clock;
    // (motion: the opener's body's, the game's player's BtActStatus or the partner's own)
    const int motion = opener == ActorNumber(0) ? (int) BtActStatus.motion_no : g_partner.motion;
    // (lit: the players whose bodies the Atla's light was last drawn on here, comma-joined, 0 none)
    char lit[4 * kMaxPlayers + 2] = "";
    int  at = 0;

    for (int i = 0; i < kMaxPlayers; ++i) {
        if (g_lit[i] > 0.0f) {
            at += std::snprintf(lit + at, sizeof lit - at, "%s%d", at > 0 ? "," : "", i + 1);
        }
    }
    std::fprintf(stderr, "coop-opener: test: p%d clock %lld task %d presenting %d engine %d mesoff %d motion %d held %d atla %d lit %s\n", NetplayLocalPlayer(), clock,
                 (int) gameTask, (int) g_present[opener - 1].kind, g_opener_engine.player != 0 && !g_opener_engine.done ? 1 : 0, DngMessMan.enabled ? 0 : 1,
                 motion, held ? 1 : 0, (int) atraGetStatus, at > 0 ? lit : "0");
}

PresentSetScope::PresentSetScope(bool take) : take_(take) {
    const Engine &e = g_opener_engine;

    if (e.player == 0 || e.done || (!take && Mutated("present_screen_lost"))) {
        return;
    }
    ScreenSave(g_screen_kept);
    kept_ = true;
    if (take) { // the set gets the screen of a copy with no presentation on it
        ScreenKept none;

        none.map = infoMap == 0 ? infoMapOld : infoMap;
        ScreenPut(none);
        std::fprintf(stderr, "coop-opener: a state set taken beside player %d's presentation: its screen left out (box %d %d, Atla %d, window %d)\n",
                     e.player, (int) g_screen_kept.small, (int) g_screen_kept.big, (int) g_screen_kept.atla, g_screen_kept.window ? 1 : 0);
    }
}

PresentSetScope::~PresentSetScope() {
    if (!kept_) {
        return;
    }
    ScreenPut(g_screen_kept);
    if (!take_) {
        std::fprintf(stderr, "coop-opener: a state set applied beside player %d's presentation: its screen kept\n", g_opener_engine.player);
    }
}

int PresentTestStateDue() {
    const int due = g_reload_due;

    g_reload_due = 0;
    return due;
}

std::uint32_t PresentHash() {
    std::uint32_t bits = 0;

    for (int i = 0; i < kMaxPlayers; i++) {
        bits |= (std::uint32_t) (g_present[i].kind & 3) << (2 * i);
    }
    return bits;
}

void PresentResetForSession() {
    EngineClear("session reset");
    for (Present &p : g_present) {
        p = Present{};
    }
}

} // namespace coop
