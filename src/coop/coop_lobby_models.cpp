#include "coop/game_ns.hpp"
#include "coop/coop_lobby_models.hpp"

#include <libvu0.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <array>
#include <new>
#include <vector>

#include "btitem.hpp"
#include "btmisc.hpp"
#include "character.hpp"
#include "common.h"
#include "dataalloc.hpp"
#include "dataread.hpp"
#include "dun/gameloop.hpp" // UserStatus
#include "render/draw3d.hpp"
#include "frame.hpp"
#include "frameattr.hpp"
#include "gameutil.hpp"
#include "gfx/gfx.hpp"
#include "itemdata.hpp"
#include "mglib.hpp"
#include "render/gs_state.hpp"
#include "renderinfo.hpp"
#include "savedata.hpp"
#include "rect.hpp"
#include "render/screen.hpp"
#include "snd.hpp"
#include "sound_effect.h"
#include "texture.hpp"
#include "textureanime.hpp"
#include "userstatus.hpp"

#include "coop/coop.hpp"
#include "coop/coop_audio.hpp"  // SoundCoopLoadVoice, SoundCoopVoice
#include "coop/coop_combat.hpp" // StarterWeapon
#include "coop/netplay.hpp"     // NetplayTestWritePng (the sheet)
#include "coop/coop_core.hpp"   // kModelQuads, kPackBytes, g_partner (its voice set, put back at Close)

namespace dc::game {
int keyCtrl(float x, float y, MOTION_INFO *motion);
extern const std::array<const char *, 6> charaNameTbl; // btitem.cpp: each member's .chr pack
extern std::array<SND_SE_INFO *, 11>     voice_info;   // repin: snd.cpp's, out of its anonymous namespace (recipe 0052)
} // namespace dc::game

namespace coop::lobbymodels {

namespace {

constexpr int kMembers = 6;

// A texture block each, body and weapon together (the weapon never changes in the lobby). Floor-only blocks of co-op's
// (0x3A the Atla's, 0x3C the stand-in's, 0x3D the item's) and three nothing loads into: the lobby is on the title, where
// none is in use, and Close empties them before the game starts.
constexpr int kBlocks[kMembers] = {0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F};

// The idle shot (Jep, 2026-10-31: not dead on but about 45 degrees round and angled up; dynamic poses, as the box art's, from
// the attack motions with non-standard camera angles): each member held on one frame of its swing, with a camera of its own.
// stage: which of its swing's motions (kSwing); at: how far into it (0 its first frame, 1 its last); azimuth: the camera round
// from the model's front (radians, + to the model's left); rise: the eye above (+) or below (-) the shot's centre, as a share
// of the distance; roll: the camera's tilt (radians).
struct HeroShot {
    int   stage;
    float at;
    float azimuth;
    float rise;
    float roll;
    int   motion = -1; // a motion of the member's own by number, in place of its swing's (-1: the swing's)
    const char *aims = nullptr; // bones turned on top of it, as DC_LOBBY_MODELS_AIM writes them (a pose no motion has)
    float       orb = 0.0f;     // a glowing ball of this radius between its two hands (r_hand, l_hand), 0 none
    const char *pack = nullptr; // the member's model and motions from another file (a story scene's copy), null its dungeon one
    int         face = -1;      // that model's face animation to show (its TEX_ANIME number: a wink), -1 none
    // The scene model itself drawn while the hero shot shows, its textures in ShowBlock (Jep, 2026-11-01: Ruby's wink, her
    // e228 model whole, its own face sheet), crossed over to the body (the dungeon model, every run and swing) with the
    // lock's camera move and back with the unlock's. Off: the scene model only lends the body its pose.
    bool        show = false;
    const char *shot_frame = nullptr; // the hero shot centred on this frame of the shown model (null: the pose's extent)
    float       shot_half = 0.0f;     // that shot's half height, x the model's height
    float       shot_up = 0.0f;       // its centre above the frame, x the height
    short       eyes_x = -1, eyes_y = -1; // the face animation held on the eye strip at this place on the sheet (a wink held)
    // The moving shot (Jep, 2026-11-02: a moving model once selected): motion plays and loops in place of the held frame,
    // after enter played once (-1 none). Story scene motions by their number (a set's: set x 100 + its key, as the
    // converter's names end: Toan's macho_sword_get_loop_103).
    bool        loop = false;
    int         enter = -1;
    // A story scene's motion-only file (Toan's: gedit/e03/chara/e232c01d.chr, configuration e232c01d.cfg), its motions
    // loaded onto the body as the scene's _LOAD_CHARA does: the same skeleton, no model of its own.
    const char *moves = nullptr;
    const char *moves_cfg = nullptr;
    // The hero shot's own lens (Jep, 2026-11-03: Toan's guard framed in Blender, close and wide): the projection's scale
    // in the title's pixels (750 its own; smaller is wider), eased back to the title's with the lock. 0: the title's.
    float       lens = 0.0f;
    // The hero shot's camera placed outright (Jep, 2026-11-03: Toan's card as his Blender view): the eye at eye_off from
    // this frame of the body (model space, y up, its face +z), looking along eye_look, no roll; eased to the usual camera
    // with the lock. Null: the camera from azimuth, rise and the shot.
    const char *eye_frame = nullptr;
    float       eye_off[3] = {};
    float       eye_look[3] = {};
    // (The body itself is always the dungeon model: every run, swing and combo plays its motions by their dungeon numbers.
    // Jep's 2026-11-04 playtest: Xiao's card body from her e04c04b scene file swung with that scene's motion 11, a purr.)
};

// The shown scene models' textures (HeroShot::show), a block each, unused on the title: Ruby's in 0x45 (the floors' key
// window), Xiao's in 0x42 (the partner's weapon ring, loaded again wherever its block is empty).
int ShowBlock(int chara) {
    return chara == 1 ? 0x42 : 0x45;
}
HeroShot g_hero[6] = {
    // Toan (Jep, 2026-11-03: his guard, the camera as his Blender view, "close enough for now"): the guard (motion 8, then
    // its loop 9) seen from his left at his neck's height, the dagger framing his face and his open hand in the card, a
    // wide lens (177); then (Jep, 04:20) slid so his fingers near the card's left and his hat its top, and turned 3 degrees
    // toward him. The lock's camera from the waist-up shot on his neck. (Before: 30% into the swing, 35 degrees round,
    // from below, tilted 8 degrees.)
    {0, 0.50f, 0.192f, 0.05f, 0.0f, 9, nullptr, 0.0f, nullptr, -1, false, "jnt11_1", 0.30f, -0.075f, -1, -1, true, 8, nullptr, nullptr, 177.0f,
     "jnt11_1", {2.133f, -0.236f, 12.183f}, {-0.186f, -0.03f, -0.982f}},
    // Xiao (Jep, 2026-11-03: her first human scene's jumping twirl, posed and framed in his Blender): that scene's model
    // (e04c04b) shown whole, its motion 10 held 29% in, arm and claws up at the top of the jump, seen a little from below
    // on her left, lens 114; crossed over to her dungeon body with the lock, as Ruby's. (Before: the box art's attack,
    // her shot, motion 13 at 80%.)
    {0, 0.288f, 0.61f, -0.10f, 0.0f, 10, nullptr, 0.0f, "gedit/e01/chara/e04c04b.chr", -1, true, nullptr, 0.0f, 0.0f, -1, -1, false, -1, nullptr, nullptr,
     114.0f, "jnt11_1", {-2.948f, 2.585f, 11.31f}, {0.2285f, -0.3289f, -0.9163f}},
    {0, 0.30f, 0.61f, -0.22f, -0.14f},
    // Ruby (Jep, 2026-11-01: as she joins, "Ruby has become an ally": a wave, palm out): her Queens event model
    // (gedit/e03/chara/e223c05a.chr, the join scene's), its motion 7 at 30%, 20 degrees round. (The orb reach she held before:
    // "3:ik:r_arm,r_farm,r_hand=-1.8,11.0,3.2,-6,12,-3;3:ik:l_arm,l_farm,l_hand=-1.6,8.9,3.2,4,7,-1" with orb 1.5.)
    // (2026-11-01, Jep: "ruby wink g is much better", the lobby's proof of concept): her Queens model e228c05a whole, its
    // wink loop (motion 17) halfway, the camera 135 degrees round (the motion faces away), a little below, 4 degrees of
    // roll, the shot on her neck waist up, the wink strip (128,34) held on her face animation 2.
    {0, 0.50f, 2.356f, -0.25f, 0.0698f, 17, nullptr, 0.0f, "gedit/e03/chara/e228c05a.chr", 2, true, "neck", 0.26f, -0.03f, 128, 34},
    {0, 0.30f, 0.61f, -0.22f, -0.14f}, {0, 0.30f, 0.61f, -0.22f, -0.14f},
};

// Ticks of the lock's camera move and of the unlock's ease back (Jep: about half a second).
constexpr float kEaseTicks = 30.0f;

// Each member's attack motions, as coop_combat's tap (kTapMotions): Toan, Xiao (wind-up, throw), Goro, Ruby, Ungaga,
// Osmond; -1 past the last.
constexpr int kSwing[kMembers][2] = {{0x24, -1}, {11, 13}, {0x24, -1}, {11, -1}, {0x25, -1}, {11, -1}};

// The last motion of a melee member's combo string (charakey.cpp's ToanKey: 0x24, then 0x25 to 0x28).
constexpr int kComboLast = 0x28;

// The voice cues of a swing, as coop_audio plays the partner's (kStartCue, kStrikeCue): at its start and where it strikes.
constexpr int kStartCue[kMembers] = {-1, SE_CHARA_ACTION, -1, -1, -1, -1};
constexpr int kStrikeCue[kMembers][2] = {
    {SE_CHARA_ACTION, SE_CHARA_SHOUT}, {SE_CHARA_ACTION_2, SE_CHARA_SHOUT_2}, {SE_CHARA_ACTION, SE_CHARA_SHOUT},
    {SE_CHARA_ACTION, SE_CHARA_SHOUT}, {SE_CHARA_ACTION, SE_CHARA_SHOUT},     {SE_CHARA_ACTION, SE_CHARA_SHOUT},
};

// The sword combos' own cues, each hit its own (Jep's 2026-11-04 playtest: every hit of Toan's string sounded alike), at
// the motion times retail plays them: charakey.cpp's ToanKey_Play (a swing and a shout a hit, 0x24 to 0x28) and
// UngagaKey_Play (a shout a hit, 0x25 to 0x27). The members without one strike with kStrikeCue.
struct ComboCue {
    int   chara;
    float at;
    int   se_no;
};
constexpr ComboCue kComboCues[] = {
    {0, 825.0f, SE_CHARA_ACTION},   {0, 825.0f, SE_CHARA_SHOUT},   {0, 833.0f, SE_CHARA_ACTION_2}, {0, 833.0f, SE_CHARA_SHOUT_2},
    {0, 842.0f, SE_CHARA_ACTION},   {0, 842.0f, SE_CHARA_SHOUT_2}, {0, 852.0f, SE_CHARA_ACTION},   {0, 852.0f, SE_CHARA_SHOUT},
    {0, 864.0f, SE_CHARA_ACTION_2}, {0, 864.0f, SE_CHARA_SHOUT_3}, {4, 675.0f, SE_CHARA_SHOUT},    {4, 687.0f, SE_CHARA_SHOUT_2},
    {4, 700.0f, SE_CHARA_SHOUT_2},
};

// The ground the run's footsteps sound on: the first kind (no floor under a model in the void).
constexpr int kFootKind = 0;

struct Well {
    // Kept from the first Open on (Close zeroes the model memory instead of freeing it).
    CCharacter    *body = nullptr;
    CCharacter    *weapon = nullptr;
    CTexAnimeData *tex_anime = nullptr;
    u_char        *model = nullptr;
    CDataAlloc2<1> arena;
    // The hero shot's pose from a story scene's model (HeroShot::pack): loaded beside the body, never drawn, stepped on
    // the held frame, its bones copied onto the body's by name. Kept as the body's are.
    CCharacter    *pose = nullptr;
    u_char        *pose_model = nullptr;
    CTexAnimeData *pose_anime = nullptr; // its face animations (a wink), which write into the body's face texture
    CDataAlloc2<1> pose_arena;
    bool           posed = false;

    bool  loaded = false;
    bool  weapon_loaded = false;
    float height = 14.0f; // to the top of the head, measured on the first idle pose
    float centre[3] = {}, reach_up = 0.0f, reach_side = 0.0f; // the pose as held this tick (the hero shot frames it)
    float top = 0.0f;         // its highest frame (a hat, a propeller), which the hero shot keeps in
    float follow[2] = {};     // where the camera looks across the ground, eased after the pose (a lunge, a run)
    bool  followed = false;

    bool  locked = false;
    bool  silhouette = false; // a member not yet reached in the story: all black (SetSilhouette)
    float shade = 1.0f;       // its lights' brightness, 0 black .. 1 full (SetShade: an unselected card darkened)
    float azimuth = 0.0f;     // where the camera stands round the model this tick (0: in front), for the stick
    float zoom = 0.0f;     // 0 the upper-body shot, 1 the whole body, moved kEaseTicks to cover it
    float yaw = 0.0f;      // 0 faces the camera
    float ease_from = 0.0f; // the unlock's ease back: the heading it started from, and its ticks so far
    int   ease_tick = -1;
    int   zoom_ticks = -1;   // ticks since the lock or unlock, until the camera arrives (the log's ease length)
    float ease_step = 0.0f;  // the largest heading change of one tick on the way back

    float stick_x = 0.0f, stick_y = 0.0f;
    bool  attack = false, attack_was = false;
    bool  attacking = false;
    int   stage = 0, swing_motion = -1, swing_ticks = 0;
    bool  struck = false;
    float cue_time = 0.0f; // the swing's motion time its combo cues were played to (kComboCues)
    bool  queued = false; // Cross pressed during a swing: the combo's next hit follows it
    int   hits = 0;       // hits of the string so far

    int   sound_motion = -1; // the motion events last stepped from, and the time they reached
    float sound_time = 0.0f;

    int loop_phase = 0; // the moving shot's (StepLoop): 0 its enter motion, 1 its loop; back to 0 with each lock
};

struct LobbyModels {
    Well   wells[kMembers];
    u_int *pack = nullptr; // one load's file at a time, kept with the models
    int    open = 0;
    int    voice_set = -1; // the voice set the lobby loaded into the partner's port, -1 none
} g_lobby_models;

int Chara(const Well &well) {
    return static_cast<int>(&well - g_lobby_models.wells);
}

Well *Find(int member) {
    if (member < 0 || member >= kMembers || !g_lobby_models.wells[member].loaded) {
        return nullptr;
    }
    return &g_lobby_models.wells[member];
}

int BlockTextures(int block) {
    int count = 0;

    for (const CTexture &texture : TexManager.textures) {
        count += texture.name[0] != 0 && texture.block == block ? 1 : 0;
    }
    return count;
}

void EmptyBlock(int block) {
    TexManager.DeleteTextureBlock(block);
    TexManager.CleanUpBuffer();
    TexManager.CleanUpTextureList();
}

bool HasMotion(const Well &well, int motion) {
    if (motion < 0) {
        return false;
    }
    // (by the sets: a story scene's motions loaded on top number from 100)
    const MOTION_INFO *info = well.body->GetMotionInfo(motion);
    return info != nullptr && info->end > info->start && info->start >= 0;
}

void PlayMotion(Well &well, int motion, int flags) {
    well.body->motion_no = motion;
    well.body->motion_flags = flags;
    well.body->motion_speed = -1.0f;
}

// The moving shot (HeroShot::loop) on one model, the body or a scene's pose model: its enter motion played to its end
// (flag 2 holds the last frame, motion_state 3 says it got there), then the loop (flags 0). phase: 0 entering, 1 looping.
void StepLoop(CCharacter *model, const HeroShot &hero, int &phase) {
    if (phase == 0 && hero.enter >= 0 && model->GetMotionInfo(hero.enter) != nullptr) {
        if (model->motion_no != hero.enter) {
            model->motion_no = hero.enter;
            model->motion_flags = 2 | 4;
            model->motion_speed = -1.0f;
            return;
        }
        if (model->motion_state != 3) {
            return;
        }
    }
    phase = 1;
    if (model->motion_no != hero.motion && model->GetMotionInfo(hero.motion) != nullptr) {
        model->motion_no = hero.motion;
        model->motion_flags = 0;
        model->motion_speed = -1.0f;
    }
}

// The weapon item a member holds: the one asked for, the save's equipped one when its status has the member, else its
// starting weapon (StarterWeapon, the stand-in rule). how says which, for the log.
int WeaponItem(int chara, const int *asked, const char **how) {
    if (asked != nullptr && *asked > 0) {
        *how = "asked";
        return *asked;
    }

    CUserStatus *status = UserStatus != nullptr ? UserStatus : SaveData != nullptr ? SaveData->GetDngStatus() : nullptr;

    if (status != nullptr && chara < status->party_size) {
        int slot = status->equipped_weapon_slot[chara];

        if (slot >= 0 && slot < 11 && status->chara_weapons[chara][slot].item_no > 0) {
            *how = "save";
            return status->chara_weapons[chara][slot].item_no;
        }
    }

    *how = "starter";
    return StarterWeapon(chara);
}

// As coop_party's LoadWeapon: the weapon's model file into the hand's "weapon" frame, here in the body's own block.
void LoadWeapon(Well &well, int chara, const int *asked) {
    static const int first[kMembers] = {ITEM_WEAPON_DAGGER_BROKEN,    ITEM_WEAPON_WOODENSLINGSHOT_BROKEN, ITEM_WEAPON_MALLET_BROKEN,
                                        ITEM_WEAPON_GOLD_RING_BROKEN, ITEM_WEAPON_FIGHTING_STICK_BROKEN,  ITEM_WEAPON_MACHINE_GUN_BROKEN};
    const char *how = "";
    int         item = WeaponItem(chara, asked, &how);
    int         model_no = 1;

    if (item >= first[chara] && item - first[chara] < 64) {
        model_no = item - first[chara];
    }

    char name[64];
    char cfg[64];
    char path[128];
    int  size = 0;

    BtGetWeaponNamePath2(name, cfg, chara, model_no);
    std::snprintf(path, sizeof path, "commenu/weapon/%s", name);

    if (LoadFile(path, g_lobby_models.pack, &size) == 0 || size <= 0 || size > kPackBytes) {
        std::fprintf(stderr, "lobby-models: member %d cannot load weapon %s\n", chara, path);
        return;
    }

    CFrame *hand = well.body->frame->SearchFrame((char *) "weapon");

    well.weapon->Initialize();
    well.weapon->LoadPackData3(g_lobby_models.pack, cfg, &well.arena, kBlocks[chara], &well.arena, 1, 0);

    if (well.weapon->frame == nullptr || hand == nullptr) {
        std::fprintf(stderr, "lobby-models: member %d (%s) cannot hold %s\n", chara, charaNameTbl[chara], path);
        return;
    }

    well.weapon->SetPosition(0.0f, 0.0f, 0.0f);
    well.weapon->SetRotation(0.0f, 0.0f, 0.0f);
    well.weapon->frame->SetReference(hand);
    well.weapon_loaded = true;
    std::fprintf(stderr, "lobby-models: member %d weapon %s (item %d, %s)\n", chara, path, item, how);
}

float TopOf(CFrame *frame, float top) {
    for (; frame != nullptr; frame = frame->brother) {
        sceVu0FMATRIX world;

        frame->GetLWMatrix(world);
        top = std::max(top, world[3][1]);
        top = TopOf(frame->child, top);
    }
    return top;
}

void Places(CFrame *frame, std::vector<std::array<float, 3>> &out) {
    for (; frame != nullptr; frame = frame->brother) {
        sceVu0FMATRIX world;

        frame->GetLWMatrix(world);
        out.push_back({world[3][0], world[3][1], world[3][2]});
        Places(frame->child, out);
    }
}

// The pose's middle (the mean of its frames: a lunge carries it with the body) and how far up or down and to the side most
// of it reaches (the 85th percentile, so a lone helper frame far off does not shrink the shot).
void Measure(Well &well) {
    static std::vector<std::array<float, 3>> places;
    std::vector<float>                      ups;
    std::vector<float>                      sides;

    places.clear();
    Places(well.body->frame, places);
    if (places.empty()) {
        return;
    }
    for (int i = 0; i < 3; i++) {
        well.centre[i] = 0.0f;
        for (const auto &place : places) {
            well.centre[i] += place[i] / (float) places.size();
        }
    }
    for (const auto &place : places) {
        ups.push_back(std::fabs(place[1] - well.centre[1]));
        sides.push_back(std::hypot(place[0] - well.centre[0], place[2] - well.centre[2]));
    }
    const size_t at = places.size() * 85 / 100;
    const size_t side_at = places.size() * 70 / 100; // (sideways a model may carry a few far helper frames: Ruby's spell points)

    std::nth_element(ups.begin(), ups.begin() + at, ups.end());
    std::nth_element(sides.begin(), sides.begin() + side_at, sides.end());
    well.reach_up = ups[at];
    well.reach_side = sides[side_at];
    well.top = -1.0e9f;
    // (only frames near the body: some models carry helper frames far off to the side, Ruby's spell points among them)
    for (const auto &place : places) {
        if (std::hypot(place[0] - well.centre[0], place[2] - well.centre[2]) <= 2.0f * well.reach_side + 1.0f) {
            well.top = std::max(well.top, place[1]);
        }
    }
}

// The frame tree with each frame's world place, once a member (DC_LOBBY_MODELS_BONES set): the names to turn.
void ListFrames(CFrame *frame, int depth, int member) {
    for (; frame != nullptr; frame = frame->brother) {
        sceVu0FMATRIX world;

        frame->GetLWMatrix(world);
        std::fprintf(stderr, "lobby-models: member %d frame %*s%s at (%.1f %.1f %.1f)\n", member, depth * 2, "", frame->name, world[3][0], world[3][1],
                     world[3][2]);
        ListFrames(frame->child, depth + 1, member);
    }
}

// DC_LOBBY_MODELS_BONES=<member>:<frame>=<x>,<y>,<z>[;<member>:<frame>=...] (dev looks: a pose the motions lack, Jep
// 2026-11-01): after the held frame, each named frame turned by those degrees about its own axes (X, then Y, then Z).
void TurnBones(Well &well) {
    static const char *spec = TestEnv("DC_LOBBY_MODELS_BONES");
    static unsigned    listed = 0;
    const int          chara = Chara(well);

    if (spec == nullptr || well.locked) {
        return;
    }
    if ((listed & (1u << chara)) == 0) {
        listed |= 1u << chara;
        ListFrames(well.body->frame, 0, chara);
    }
    for (const char *at = spec; at != nullptr && *at != 0;) {
        int   member = -1;
        char  name[64] = "";
        float x = 0.0f, y = 0.0f, z = 0.0f;

        if (std::sscanf(at, "%d:%63[^=]=%f,%f,%f", &member, name, &x, &y, &z) == 5 && member == chara) {
            if (CFrame *frame = well.body->frame->SearchFrame(name)) {
                sceVu0FMATRIX turn;

                sceVu0UnitMatrix(turn);
                sceVu0RotMatrixX(turn, turn, x * 0.0174533f);
                sceVu0RotMatrixY(turn, turn, y * 0.0174533f);
                sceVu0RotMatrixZ(turn, turn, z * 0.0174533f);
                sceVu0MulMatrix(frame->local, frame->local, turn);
                frame->world_valid = false;
            }
        }
        at = std::strchr(at, ';');
        at = at != nullptr ? at + 1 : nullptr;
    }
}

// a * b, column-major ([column][row]) as the frames keep them.
void Mul(sceVu0FMATRIX out, const sceVu0FMATRIX a, const sceVu0FMATRIX b) {
    sceVu0FMATRIX m;

    for (int c = 0; c < 4; c++) {
        for (int r = 0; r < 4; r++) {
            m[c][r] = a[0][r] * b[c][0] + a[1][r] * b[c][1] + a[2][r] * b[c][2] + a[3][r] * b[c][3];
        }
    }
    std::memcpy(out, m, sizeof m);
}

// DC_LOBBY_MODELS_AIM=<member>:<bone>><next>=<x>,<y>,<z>[;...] (dev looks: a pose the motions lack): after the held frame,
// each bone turned so the line from it to the next joint points along (x, y, z) in the model's own space (y up, its face
// +z, its right -x), in the order given (an upper arm before its forearm). Logs how near each came.
float Smooth(float t);

void AimSpec(Well &well, const char *spec, float weight) {
    const int chara = Chara(well);

    for (const char *at = spec; at != nullptr && *at != 0;) {
        int   member = -1;
        char  bone_name[64] = "", next_name[64] = "";
        float want[3] = {};

        char  limb[3][32] = {};
        float pole[3] = {};

        // <member>:ik:<upper>,<lower>,<end>=<x>,<y>,<z>,<px>,<py>,<pz> (as MoonMod's poses reach: a hand to a place with an
        // elbow hint): the end joint taken to (x, y, z) in the model's space, the middle joint bending toward (px, py, pz),
        // the two bones aimed as the lines below do. Their lengths are kept, so the limb stays whole.
        if (std::sscanf(at, "%d:ik:%31[^,],%31[^,],%31[^=]=%f,%f,%f,%f,%f,%f", &member, limb[0], limb[1], limb[2], &want[0], &want[1], &want[2], &pole[0],
                        &pole[1], &pole[2]) == 10 &&
            member == chara) {
            CFrame *joint[3] = {well.body->frame->SearchFrame(limb[0]), well.body->frame->SearchFrame(limb[1]), well.body->frame->SearchFrame(limb[2])};

            if (joint[0] != nullptr && joint[1] != nullptr && joint[2] != nullptr) {
                sceVu0FMATRIX m[3];
                float         p[3][3];

                for (int j = 0; j < 3; j++) {
                    joint[j]->GetLWMatrix(m[j]);
                    p[j][0] = m[j][3][0];
                    p[j][1] = m[j][3][1];
                    p[j][2] = m[j][3][2];
                }
                const auto  length = [](const float *a, const float *b) { return std::hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]); };
                const float upper = length(p[0], p[1]);
                const float lower = length(p[1], p[2]);
                float       to[3] = {want[0] - p[0][0], want[1] - p[0][1], want[2] - p[0][2]};
                float       reach = std::hypot(to[0], to[1], to[2]);

                for (float &v : to) {
                    v /= reach;
                }
                reach = std::clamp(reach, std::fabs(upper - lower) + 0.01f, upper + lower - 0.01f);
                const float along = (upper * upper - lower * lower + reach * reach) / (2.0f * reach);
                const float out = std::sqrt(std::max(0.0f, upper * upper - along * along));
                float       bend[3] = {pole[0] - p[0][0], pole[1] - p[0][1], pole[2] - p[0][2]};
                const float on = bend[0] * to[0] + bend[1] * to[1] + bend[2] * to[2];

                for (int i = 0; i < 3; i++) {
                    bend[i] -= on * to[i];
                }
                const float bl = std::max(1.0e-4f, std::hypot(bend[0], bend[1], bend[2]));
                float       elbow[3], end[3];

                for (int i = 0; i < 3; i++) {
                    elbow[i] = p[0][i] + to[i] * along + bend[i] / bl * out;
                    end[i] = p[0][i] + to[i] * reach;
                }
                char line[2][200];

                std::snprintf(line[0], sizeof line[0], "%d:%s>%s=%f,%f,%f", member, limb[0], limb[1], elbow[0] - p[0][0], elbow[1] - p[0][1],
                              elbow[2] - p[0][2]);
                std::snprintf(line[1], sizeof line[1], "%d:%s>%s=%f,%f,%f", member, limb[1], limb[2], end[0] - elbow[0], end[1] - elbow[1], end[2] - elbow[2]);
                for (const char *one : line) {
                    AimSpec(well, one, weight);
                }
            }
            // <member>:<chain>@<joint>: the chain's root moved onto that joint's place, turned as it was (Xiao's rig: a hand or
            // a foot is a chain of its own that only sits on its limb's end joint, so it stays behind when the limb turns).
        } else if (std::sscanf(at, "%d:%63[^@;]@%63[^;]", &member, bone_name, next_name) == 3 && member == chara) {
            CFrame *chain = well.body->frame->SearchFrame(bone_name);
            CFrame *joint = well.body->frame->SearchFrame(next_name);

            if (chain != nullptr && joint != nullptr && chain->parent != nullptr) {
                sceVu0FMATRIX world, joint_world, parent_world, parent_inverse;

                chain->GetLWMatrix(world);
                joint->GetLWMatrix(joint_world);
                for (int i = 0; i < 3; i++) {
                    world[3][i] += (joint_world[3][i] - world[3][i]) * weight;
                }
                chain->parent->GetLWMatrix(parent_world);
                sceVu0InversMatrix(parent_inverse, parent_world);
                Mul(chain->local, parent_inverse, world);
                chain->world_valid = false;
            }
        } else if (std::sscanf(at, "%d:%63[^>]>%63[^=]=%f,%f,%f", &member, bone_name, next_name, &want[0], &want[1], &want[2]) == 6 && member == chara) {
            CFrame *bone = well.body->frame->SearchFrame(bone_name);
            CFrame *next = well.body->frame->SearchFrame(next_name);

            if (bone != nullptr && next != nullptr && bone->parent != nullptr) {
                sceVu0FMATRIX world, next_world, parent_world, parent_inverse, turn, turned;

                bone->GetLWMatrix(world);
                next->GetLWMatrix(next_world);
                float have[3] = {next_world[3][0] - world[3][0], next_world[3][1] - world[3][1], next_world[3][2] - world[3][2]};
                const float hl = std::sqrt(have[0] * have[0] + have[1] * have[1] + have[2] * have[2]);
                const float wl = std::sqrt(want[0] * want[0] + want[1] * want[1] + want[2] * want[2]);

                for (int i = 0; i < 3; i++) {
                    have[i] /= hl;
                    want[i] /= wl;
                }
                float       axis[3] = {have[1] * want[2] - have[2] * want[1], have[2] * want[0] - have[0] * want[2], have[0] * want[1] - have[1] * want[0]};
                const float length = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
                const float angle = std::atan2(length, have[0] * want[0] + have[1] * want[1] + have[2] * want[2]) * weight;
                const float s = std::sin(angle);
                const float c = std::cos(angle);

                if (length > 1.0e-5f) {
                    for (float &v : axis) {
                        v /= length;
                    }
                    // Rodrigues: the turn taking have onto want, about the bone's own place.
                    const float t = 1.0f - c;
                    const float x = axis[0], y = axis[1], z = axis[2];
                    const float r[3][3] = {{t * x * x + c, t * x * y - s * z, t * x * z + s * y},
                                           {t * x * y + s * z, t * y * y + c, t * y * z - s * x},
                                           {t * x * z - s * y, t * y * z + s * x, t * z * z + c}};

                    sceVu0UnitMatrix(turn);
                    for (int col = 0; col < 3; col++) {
                        for (int row = 0; row < 3; row++) {
                            turn[col][row] = r[row][col];
                        }
                    }
                    sceVu0CopyMatrix(turned, world);
                    turned[3][0] = turned[3][1] = turned[3][2] = 0.0f;
                    Mul(turned, turn, turned);
                    turned[3][0] = world[3][0];
                    turned[3][1] = world[3][1];
                    turned[3][2] = world[3][2];
                    bone->parent->GetLWMatrix(parent_world);
                    sceVu0InversMatrix(parent_inverse, parent_world);
                    Mul(bone->local, parent_inverse, turned);
                    bone->world_valid = false;
                }
                bone->GetLWMatrix(world);
                next->GetLWMatrix(next_world);
                float now[3] = {next_world[3][0] - world[3][0], next_world[3][1] - world[3][1], next_world[3][2] - world[3][2]};
                const float nl = std::sqrt(now[0] * now[0] + now[1] * now[1] + now[2] * now[2]);
                const float dot = (now[0] * want[0] + now[1] * want[1] + now[2] * want[2]) / nl;

                if (static int said = 0; said++ < 16) {
                    std::fprintf(stderr, "lobby-models: member %d aims %s at (%.2f %.2f %.2f): %.1f degrees off\n", chara, bone_name, want[0], want[1],
                                 want[2], std::acos(std::clamp(dot, -1.0f, 1.0f)) * 57.2958f);
                }
            }
        }
        at = std::strchr(at, ';');
        at = at != nullptr ? at + 1 : nullptr;
    }
}

void AimBones(Well &well) {
    static const char *asked = TestEnv("DC_LOBBY_MODELS_AIM");
    const char        *spec = asked != nullptr ? asked : g_hero[Chara(well)].aims;
    // Eased out with the lock's camera and back in with the unlock's (no snap): the turns at this share.
    const float        weight = 1.0f - Smooth(well.zoom);

    if (spec != nullptr && weight > 0.0f) {
        AimSpec(well, spec, weight);
    }
}

// The skinned meshes are bent in the motion's step from each frame's local matrix (MotionProc2, the proc_list2 pass): a
// frame turned after the step moves its rigid pieces at draw but leaves the skin bent as before (Jep, 2026-11-01: "a
// handless arm and a floating hand"). With turns made, that pass again, from the turned frames.
struct PoseLine;
const std::vector<PoseLine> &PoseLines(int chara);

void Reskin(Well &well) {
    static const bool bones = TestEnv("DC_LOBBY_MODELS_BONES") != nullptr;
    static const bool aims = TestEnv("DC_LOBBY_MODELS_AIM") != nullptr;

    if (!bones && !aims && g_hero[Chara(well)].aims == nullptr && !well.posed && PoseLines(Chara(well)).empty()) {
        return;
    }

    int             index = -1;
    tagMOTION_TYPE *motion = well.body->motion_no >= 0 ? well.body->GetMotionParam(well.body->motion_no, &index, nullptr, nullptr, nullptr) : nullptr;

    if (motion == nullptr) {
        return;
    }
    for (Mot_List *list = motion->proc_list2; list != nullptr;) {
        list = MotionProc2(well.body->frame, motion, motion->frame_info, list);
    }
}

// a + (b - a) t, a turn kept a turn: each axis straightened after (Gram-Schmidt) and given its blended length back.
void BlendLocal(sceVu0FMATRIX out, const sceVu0FMATRIX a, const sceVu0FMATRIX b, float t) {
    for (int c = 0; c < 4; c++) {
        for (int r = 0; r < 4; r++) {
            out[c][r] = a[c][r] + (b[c][r] - a[c][r]) * t;
        }
    }
    float length[3];

    for (int c = 0; c < 3; c++) {
        length[c] = std::hypot(out[c][0], out[c][1], out[c][2]);
    }
    for (int c = 0; c < 3; c++) {
        for (int k = 0; k < c; k++) {
            const float d = out[c][0] * out[k][0] + out[c][1] * out[k][1] + out[c][2] * out[k][2];

            for (int r = 0; r < 3; r++) {
                out[c][r] -= d * out[k][r];
            }
        }
        const float n = std::max(1.0e-6f, std::hypot(out[c][0], out[c][1], out[c][2]));

        for (int r = 0; r < 3; r++) {
            out[c][r] *= 1.0f / n;
        }
    }
    for (int c = 0; c < 3; c++) {
        for (int r = 0; r < 3; r++) {
            out[c][r] *= length[c];
        }
    }
}

// DC_LOBBY_MODELS_POSE=<member>:<file>[;...] (dev looks, Jep 2026-11-01: a pose set by hand in Blender): each line of the
// file "<frame> m00 .. m33" is that frame's local matrix (as a glTF node's, column by column, which is the frame's own
// layout), put on the body's frame of that name over the motion's, eased out with the lock as the other turns are.
struct PoseLine {
    char          name[32];
    sceVu0FMATRIX local;
};

const std::vector<PoseLine> &PoseLines(int chara) {
    static std::vector<PoseLine> lines[kMembers];
    static bool                  read[kMembers] = {};
    static const char           *spec = TestEnv("DC_LOBBY_MODELS_POSE");

    if (read[chara] || spec == nullptr) {
        return lines[chara];
    }
    read[chara] = true;
    for (const char *at = spec; at != nullptr && *at != '\0';) {
        int  member = -1;
        char path[260] = {};

        if (std::sscanf(at, "%d:%259[^;]", &member, path) == 2 && member == chara) {
            if (FILE *file = std::fopen(path, "r"); file != nullptr) {
                PoseLine line{};
                float   *m = &line.local[0][0];

                while (std::fscanf(file, "%31s %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f", line.name, &m[0], &m[1], &m[2], &m[3], &m[4], &m[5],
                                   &m[6], &m[7], &m[8], &m[9], &m[10], &m[11], &m[12], &m[13], &m[14], &m[15]) == 17) {
                    lines[chara].push_back(line);
                }
                std::fclose(file);
            }
            std::fprintf(stderr, "lobby-models: member %d pose file %s: %zu frames\n", chara, path, lines[chara].size());
        }
        at = std::strchr(at, ';');
        at = at != nullptr ? at + 1 : nullptr;
    }
    return lines[chara];
}

void PoseFromFile(Well &well) {
    const std::vector<PoseLine> &lines = PoseLines(Chara(well));
    const float                  weight = 1.0f - Smooth(well.zoom);

    if (lines.empty() || weight <= 0.0f) {
        return;
    }
    int missing = 0;

    for (const PoseLine &line : lines) {
        if (CFrame *frame = well.body->frame->SearchFrame(const_cast<char *>(line.name)); frame != nullptr) {
            // The turn only: each joint's offset from its parent stays the motion's (Blender's bones sit at the bind
            // pose's offsets, a few tenths off the motion's).
            sceVu0FMATRIX turn;

            std::memcpy(turn, line.local, sizeof turn);
            std::memcpy(turn[3], frame->local[3], sizeof turn[3]);
            BlendLocal(frame->local, frame->local, turn, weight);
            frame->world_valid = false;
        } else {
            missing++;
        }
    }
    if (static unsigned said = 0; missing > 0 && (said & (1u << Chara(well))) == 0) {
        said |= 1u << Chara(well);
        std::fprintf(stderr, "lobby-models: member %d pose file: %d frames not on the body\n", Chara(well), missing);
    }
}

// Each body frame below the root takes the pose model's frame of the same name, blended in at weight.
void CopyPose(CFrame *frame, CFrame *pose_root, float weight, int *copied) {
    for (; frame != nullptr; frame = frame->brother) {
        if (CFrame *from = pose_root->SearchFrame(frame->name); from != nullptr) {
            BlendLocal(frame->local, frame->local, from->local, weight);
            frame->world_valid = false;
            (*copied)++;
        }
        CopyPose(frame->child, pose_root, weight, copied);
    }
}

// The hero shot from a story scene's model (Jep, 2026-11-01: Ruby's join wave): its held frame on the body's bones,
// faded out with the lock (the body runs its own motions) and back in with the unlock.
void PoseFromScene(Well &well) {
    const float weight = 1.0f - Smooth(well.zoom);

    if (!well.posed || (weight <= 0.0f && !g_hero[Chara(well)].show)) {
        return;
    }

    const HeroShot    &hero = g_hero[Chara(well)];
    CCharacter        *pose = well.pose;
    if (hero.loop) {
        StepLoop(pose, hero, well.loop_phase);
    } else {
        const MOTION_INFO &info = pose->motion_type.motion_info[std::max(0, hero.motion)];

        pose->motion_no = std::max(0, hero.motion);
        pose->motion_flags = 1;
        pose->motion_speed = -1.0f;
        pose->motion_type.state.time = (float) info.start + hero.at * (float) (info.end - 1 - info.start);
    }
    if (hero.show) {
        sceVu0FVECTOR place = {0.0f, 0.0f, 0.0f, 1.0f};

        pose->frame->SetPosition(place);
        pose->frame->SetRotation(0.0f, well.yaw, 0.0f);
        pose->SetPosition(place);
        pose->SetRotation(0.0f, well.yaw, 0.0f);
        pose->Step();
        // A scene model's motion stands it where the scene had it (Ruby's e228: her hip 45 along and 20 up): moved so its
        // hip is the body's, the two crossing over in one place.
        CFrame *their = pose->frame->SearchFrame((char *) "hip");
        CFrame *ours = well.body->frame->SearchFrame((char *) "hip");

        if (their != nullptr && ours != nullptr) {
            sceVu0FMATRIX at, want;

            their->GetLWMatrix(at);
            ours->GetLWMatrix(want);
            sceVu0FVECTOR moved = {want[3][0] - at[3][0], want[3][1] - at[3][1], want[3][2] - at[3][2], 1.0f};

            pose->frame->SetPosition(moved);
            pose->SetPosition(moved);
            pose->Step();
        }
        return; // drawn itself (Draw); the body keeps its own idle
    }
    pose->frame->SetRotation(0.0f, 0.0f, 0.0f);
    pose->Step();

    int copied = 0;

    CopyPose(well.body->frame->child, pose->frame, weight, &copied);
    if (static unsigned said = 0; (said & (1u << Chara(well))) == 0) {
        said |= 1u << Chara(well);
        std::fprintf(stderr, "lobby-models: member %d takes %d bones from its scene pose (motion %d at %.2f)\n", Chara(well), copied, pose->motion_no,
                     hero.at);
    }
}

// A story scene's model carries its own copy of the weapon (Ruby's e228: frame "01_1_3" over "c05w01__m"), a frame of
// the root moved by the motion's keys, not hung from the hand: a turned arm leaves it where the motion put the hand
// (Jep's target 2: a ring floating at her shoulder). The weapon loaded on the hand's "weapon" frame draws in its place,
// so the scene's copy is shrunk to nothing.
bool IsWeaponMesh(const char *name) {
    const size_t n = name != nullptr ? std::strlen(name) : 0;

    return n >= 9 && name[0] == 'c' && name[3] == 'w' && std::strcmp(name + n - 3, "__m") == 0;
}

bool HoldsWeaponMesh(CFrame *frame) {
    for (; frame != nullptr; frame = frame->brother) {
        if (IsWeaponMesh(frame->name) || HoldsWeaponMesh(frame->child)) {
            return true;
        }
    }
    return false;
}

void HideSceneWeapon(Well &well) {
    for (CFrame *top = well.body->frame->child; top != nullptr; top = top->brother) {
        if (IsWeaponMesh(top->name) || HoldsWeaponMesh(top->child)) {
            for (int c = 0; c < 3; c++) {
                for (int r = 0; r < 3; r++) {
                    top->local[c][r] = 0.0f;
                }
            }
            top->world_valid = false;
        }
    }
}

void Pose(Well &well) {
    sceVu0FVECTOR place = {0.0f, 0.0f, 0.0f, 1.0f};

    well.body->frame->SetPosition(place);
    well.body->frame->SetRotation(0.0f, well.yaw, 0.0f);
    well.body->SetPosition(place);
    well.body->SetRotation(0.0f, well.yaw, 0.0f);
    well.body->Step();
    PoseFromScene(well);
    PoseFromFile(well);
    TurnBones(well);
    AimBones(well);
    Reskin(well);
    if (well.weapon_loaded) {
        HideSceneWeapon(well);
    }
    well.body->ClothStep(0); // Goro's cape hangs from his body, as the partner's does

    Measure(well);
}

// A story scene's file names its configuration info.cfg (Ruby's e228c05a) or after itself (Osmond's e402c18a.chr:
// e402c18a.cfg, Ungaga's e323_2c10a.cfg): the one the loaded pack holds.
const char *SceneConfig(const char *path) {
    static char name[80];
    const char *base = std::strrchr(path, '/');

    base = base != nullptr ? base + 1 : path;
    std::snprintf(name, sizeof name, "%.*s.cfg", static_cast<int>(std::strcspn(base, ".")), base);
    return GetPackFile(g_lobby_models.pack, (char *) "info.cfg", nullptr) != nullptr ? "info.cfg" : name;
}

// As coop_party's Load: the member's pack into its own arena and texture block, then its weapon.
bool Load(int chara, const int *weapon_item) {
    Well &well = g_lobby_models.wells[chara];

    if (well.body == nullptr) {
        well.body = new CCharacter();
        well.weapon = new CCharacter();
        well.tex_anime = new CTexAnimeData[64];
        well.model = new (std::align_val_t{64}) u_char[static_cast<size_t>(kModelQuads) * 16];
    }

    int size = 0;

    // DC_LOBBY_MODELS_PACK=<member>=<file>[;...] (dev looks): that member from another file, a story scene's copy of it.
    static const char *asked = TestEnv("DC_LOBBY_MODELS_PACK");
    static char        file[kMembers][128];
    // DC_LOBBY_MODELS_MUTATE=scene_body (l-lobby-models~scenebody): Xiao's body from her shown scene file, as 2026-11-03's card.
    const bool         scene_body = chara == 1 && g_hero[chara].pack != nullptr && TestEnvIs("DC_LOBBY_MODELS_MUTATE", "scene_body");
    const char        *path = scene_body ? g_hero[chara].pack : charaNameTbl[chara];

    for (const char *at = asked; at != nullptr && *at != 0;) {
        int  member = -1;
        char named[128];

        // (into a scratch name first: another member's entry later in the list no longer takes this one's place)
        if (std::sscanf(at, "%d=%127[^;]", &member, named) == 2 && member == chara) {
            std::strcpy(file[chara], named);
            path = file[chara];
        }
        at = std::strchr(at, ';');
        at = at != nullptr ? at + 1 : nullptr;
    }
    if (LoadFile((char *) path, g_lobby_models.pack, &size) == 0 || size <= 0 || size > kPackBytes) {
        std::fprintf(stderr, "lobby-models: cannot load %s\n", path);
        return false;
    }
    std::fprintf(stderr, "lobby-models: member %d from %s\n", chara, path);

    const int block = kBlocks[chara];

    if (int left = BlockTextures(block); left > 0) {
        std::fprintf(stderr, "lobby-models: block %#x held %d textures before member %d: emptied\n", block, left, chara);
    }
    EmptyBlock(block);

    well.arena.base = well.model;
    well.arena.buffer = well.model;
    well.arena.used = 0;
    well.arena.limit = kModelQuads;

    CCharacter *body = well.body;

    body->Initialize();
    for (int i = 0; i < 64; i++) {
        well.tex_anime[i].Initialize();
    }
    body->InitializeTexAnime(well.tex_anime, 64);
    // The dungeon packs name their configuration base.cfg, a story scene's copy as SceneConfig finds (gedit/<scene>/chara).
    body->LoadPackData2(g_lobby_models.pack, (char *) (path == charaNameTbl[chara] ? "base.cfg" : SceneConfig(path)), &well.arena, block, &well.arena, 0);

    if (body->frame == nullptr) {
        std::fprintf(stderr, "lobby-models: %s has no model\n", path);
        EmptyBlock(block);
        return false;
    }

    CFrameAttr attr;

    attr.fog_enable = false; // the void: no fog of the title's
    attr.clip_enable = false;
    attr.program_option = 0;
    body->frame->SetAttr(attr, 1, 4);
    // The motions' events are played by Sounds (footsteps on kFootKind, effects); Step's own would play them as the
    // character in control's.
    body->FootSoundEnable(1);
    body->SetFootSoundID(kFootKind);
    body->EventEnable(0);

    // The hero's story scene motions (HeroShot::moves), onto the body as the scene's _LOAD_CHARA puts them on its player:
    // the file's IMG textures into the body's block, its motions numbered on from 100 by their set.
    if (const HeroShot &hero = g_hero[chara]; hero.moves != nullptr && hero.moves_cfg != nullptr) {
        size = 0;
        if (LoadFile((char *) hero.moves, g_lobby_models.pack, &size) != 0 && size > 0 && size <= kPackBytes) {
            body->LoadPackData2(g_lobby_models.pack, (char *) hero.moves_cfg, &well.arena, block, &well.arena, 0);
        }
        const MOTION_INFO *info = hero.motion >= 0 ? body->GetMotionInfo(hero.motion) : nullptr;

        std::fprintf(stderr, "lobby-models: member %d moves from %s (%s): motion %d %s, arena %d of %d quadwords\n", chara, hero.moves,
                     hero.moves_cfg, hero.motion, info != nullptr ? "there" : "missing", well.arena.used, kModelQuads);
    }

    well = Well{well.body, well.weapon, well.tex_anime, well.model, well.arena, well.pose, well.pose_model, well.pose_anime, well.pose_arena};
    well.loaded = true;
    LoadWeapon(well, chara, weapon_item);

    // The hero shot's pose model, from its story scene's file (configuration info.cfg), motions only: its textures stay
    // out of the body's block.
    if (const char *pose_path = scene_body ? nullptr : g_hero[chara].pack; pose_path != nullptr) {
        if (well.pose == nullptr) {
            well.pose = new CCharacter();
            well.pose_model = new (std::align_val_t{64}) u_char[static_cast<size_t>(kModelQuads) * 16];
            well.pose_anime = new CTexAnimeData[64];
        }
        well.pose_arena.base = well.pose_model;
        well.pose_arena.buffer = well.pose_model;
        well.pose_arena.used = 0;
        well.pose_arena.limit = kModelQuads;
        size = 0;
        if (LoadFile((char *) pose_path, g_lobby_models.pack, &size) != 0 && size > 0 && size <= kPackBytes) {
            // Into the body's own block: the scene model's face sheet (its eyes' and mouth's frames) joins the body's textures,
            // and its face animations write into the face texture both share by name. Its other textures go unused.
            well.pose->Initialize();
            for (int i = 0; i < 64; i++) {
                well.pose_anime[i].Initialize();
            }
            well.pose->InitializeTexAnime(well.pose_anime, 64);
            // (with no face animation wanted, through a scratch block emptied at once (0x45: the floors' key window, unused on
            // the title), so the scene's own face texture never takes the body's place)
            // (shown whole: its own block, kept)
            const bool show = g_hero[chara].show;
            const int  pose_block = show ? ShowBlock(chara) : g_hero[chara].face >= 0 || TestEnv("DC_LOBBY_MODELS_FACE") != nullptr ? block : 0x45;

            if (show) {
                EmptyBlock(ShowBlock(chara));
            }
            well.pose->LoadPackData2(g_lobby_models.pack, (char *) SceneConfig(pose_path), &well.pose_arena, pose_block, &well.pose_arena, 0);
            if (pose_block != block && !show) {
                EmptyBlock(pose_block);
            }
            // DC_LOBBY_MODELS_FACE=<n> (dev looks): another of the scene model's face animations.
            if (const int face = TestEnvInt("DC_LOBBY_MODELS_FACE", g_hero[chara].face); face >= 0) {
                for (int i = 0; i < 8; i++) {
                    well.pose->TexAnimeOff(i);
                }
                well.pose->TexAnimeOn(face);
                // The wink held: the animation's first record kept on the hero's eye strip.
                if (CTexAnimeData *record = well.pose->tex_anime.first[face]; record != nullptr && g_hero[chara].eyes_x >= 0) {
                    record->duration = -1;
                    record->next = nullptr;
                    record->source_x = g_hero[chara].eyes_x;
                    record->source_y = g_hero[chara].eyes_y;
                    well.pose->tex_anime.current[face] = record;
                }
            }
            if (show && well.pose->frame != nullptr) {
                CFrameAttr shown;

                shown.fog_enable = false; // the void, as the body's
                shown.clip_enable = false;
                shown.program_option = 0;
                well.pose->frame->SetAttr(shown, 1, 4);
            }
            well.pose->EventEnable(0);
            well.pose->FootSoundEnable(0);
            well.posed = well.pose->frame != nullptr;
        }
        std::fprintf(stderr, "lobby-models: member %d posed from %s: %s\n", chara, pose_path, well.posed ? "loaded" : "not loaded");
    }

    // A body from a story scene's file with no pose model (Jep, 2026-11-01: Ruby's wink as her e228 model): its own face
    // animations, the face sheet they were made for.
    if (const int face = TestEnvInt("DC_LOBBY_MODELS_FACE", g_hero[chara].face); face >= 0 && !well.posed && path != charaNameTbl[chara]) {
        for (int i = 0; i < 8; i++) {
            body->TexAnimeOff(i);
        }
        body->TexAnimeOn(face);
        std::fprintf(stderr, "lobby-models: member %d shows its own face animation %d\n", chara, face);
        // DC_LOBBY_MODELS_FACE_HOLD=<n> (dev looks): the animation stops on its record n (a wink held, not blinked).
        // (each group's records logged: which copies which part of the face sheet where)
        const int hold = TestEnvInt("DC_LOBBY_MODELS_FACE_HOLD", -1);

        for (int group = 0; group < 24; group++) {
            int n = 0;

            for (CTexAnimeData *record = body->tex_anime.first[group]; record != nullptr && n < 32; record = record->next, n++) {
                std::fprintf(stderr, "lobby-models: face %d record %d: kind %d, %d frames, from (%d %d %dx%d) to (%d %d)\n", group, n, record->kind,
                             record->duration, record->source_x, record->source_y, record->source_width, record->source_height, record->dest_x, record->dest_y);
                if (group == face && n == hold) {
                    record->duration = -1;
                }
                if (record == body->tex_anime.last[group]) {
                    break;
                }
            }
        }
    }

    PlayMotion(well, 0, 0);
    Pose(well);
    if (float top = TopOf(body->frame, 0.0f); top > 4.0f && top < 60.0f) {
        well.height = top * 1.02f; // the highest frame of the body (a hat or a headdress counts): the crown, near enough
    }

    std::fprintf(stderr, "lobby-models: member %d (%s) loaded: block %#x, %d textures, arena %d of %d quadwords, height %.1f\n", chara,
                 path, block, BlockTextures(block), well.arena.used, kModelQuads, well.height); // (the body's own file: its runs' and swings' motions)
    return true;
}

float Smooth(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float Wrap(float angle) {
    while (angle > 3.14159265f) {
        angle -= 6.2831853f;
    }
    while (angle < -3.14159265f) {
        angle += 6.2831853f;
    }
    return angle;
}

void Voice(int chara, int se_no) {
    if (se_no < 400 || se_no >= 500 || voice_info[chara] == nullptr) {
        return;
    }
    if (g_lobby_models.voice_set != chara) {
        g_lobby_models.voice_set = SoundCoopLoadVoice(chara) ? chara : -1;
        if (g_lobby_models.voice_set < 0) {
            std::fprintf(stderr, "lobby-models: member %d has no voice set to load\n", chara);
            return;
        }
    }
    const SND_SE_INFO &info = voice_info[chara][se_no - 400];
    if (info.bank >= 0) {
        SoundCoopVoice(info.bank, info.prog, 64, 110);
    }
    std::fprintf(stderr, "lobby-models: member %d voice %d\n", chara, se_no);
}

// A member whose swings strike with retail's own combo cues (kComboCues). DC_LOBBY_MODELS_MUTATE=onecue
// (l-lobby-models~onecue): none, every hit kStrikeCue's one pair, as before.
bool HasComboCues(int chara) {
    static const bool one_cue = TestEnvIs("DC_LOBBY_MODELS_MUTATE", "onecue");

    return !one_cue && std::any_of(std::begin(kComboCues), std::end(kComboCues), [chara](const ComboCue &cue) { return cue.chara == chara; });
}

// The motion's own sound events, as coop_audio's StepVoice plays the partner's: footsteps, and effects (a voice program
// from the member's own set).
void Sounds(Well &well, const float *listener) {
    CCharacter     *body = well.body;
    int             index = -1;
    int             set_no = -1;
    tagMOTION_TYPE *motion = body->motion_no >= 0 ? body->GetMotionParam(body->motion_no, &index, nullptr, nullptr, &set_no) : nullptr;

    if (motion == nullptr) {
        well.sound_motion = -1;
        return;
    }

    const float now = motion->state.time;
    const float step = now - well.sound_time;

    if (body->motion_no == well.sound_motion && step > 0.0f && step < 2.0f) {
        for (const CHARA_EVENT &event : body->event) {
            if (event.frame < 0 || event.motion_no != set_no || static_cast<float>(event.frame) < well.sound_time ||
                static_cast<float>(event.frame) >= now) {
                continue;
            }
            if (event.kind == static_cast<s32>(CharaEventKind::SoundEffect)) {
                if (event.no >= 400 && event.no < 500) {
                    Voice(Chara(well), event.no);
                } else {
                    SndSePlay(event.no, -1, 0); // present: this copy's own (the lobby's model, on the title: no world, no other player)
                }
            } else if (event.kind == static_cast<s32>(CharaEventKind::FootSound) && body->foot_sound_id >= 0) {
                SndPlayFootSound(body->foot_sound_id, event.no, const_cast<float *>(listener));
            }
        }
    }
    well.sound_motion = body->motion_no;
    well.sound_time = now;
}

void StartSwing(Well &well, int stage, int motion = -1) {
    const int chara = Chara(well);

    well.stage = stage;
    well.swing_motion = motion >= 0 ? motion : kSwing[chara][stage];
    well.swing_ticks = 0;
    well.struck = false;
    PlayMotion(well, well.swing_motion, 4);
    well.body->motion_type.state.time = (float) well.body->motion_type.motion_info[well.swing_motion].start;
    well.cue_time = well.body->motion_type.state.time - 1.0f; // (a cue on its first frame plays)
}

void StepSwing(Well &well) {
    const int          chara = Chara(well);
    const MOTION_INFO &info = well.body->motion_type.motion_info[well.swing_motion];
    const float        time = well.body->motion_type.state.time;
    const int          next = well.stage + 1;
    const bool         last = next >= 2 || !HasMotion(well, kSwing[chara][next]);

    well.swing_ticks++;
    // Retail's own cues of this hit, as its motion passes their times (Toan's string: each hit its own swing and shout).
    if (HasComboCues(chara)) {
        for (const ComboCue &cue : kComboCues) {
            if (cue.chara == chara && cue.at >= (float) info.start && cue.at < (float) info.end && well.cue_time < cue.at && cue.at <= time) {
                Voice(chara, cue.se_no);
            }
        }
        well.cue_time = time;
    } else if (last && !well.struck && (time >= info.start + 0.4f * (float) (info.end - info.start) || well.swing_ticks >= 45)) {
        well.struck = true;
        for (int cue : kStrikeCue[chara]) {
            Voice(chara, cue);
        }
    }

    if (time >= info.end - 1.0f || well.swing_ticks >= 90) {
        if (!last) {
            StartSwing(well, next, kSwing[chara][next]);
            return;
        }
        // The string (Jep, 2026-10-31: the whole combo, not its first hit): a press during a swing queues the next, as
        // retail's ToanKey_On queues it in the swing's combo window. Melee members go on through their combo motions
        // (charakey.cpp: 0x24 to 0x28, one after another); a ranged member shoots again.
        if (well.queued) {
            const bool ranged = chara == 1 || chara == 3 || chara == 5;
            const int  following = well.swing_motion + 1;

            well.queued = false;
            if (ranged) {
                well.hits++;
                StartSwing(well, 0);
                std::fprintf(stderr, "lobby-models: member %d shoots again (hit %d)\n", chara, well.hits + 1);
                return;
            }
            if (following <= kComboLast && HasMotion(well, following)) {
                well.hits++;
                StartSwing(well, 0, following);
                std::fprintf(stderr, "lobby-models: member %d combo hit %d (motion %d)\n", chara, well.hits + 1, following);
                return;
            }
        }
        well.attacking = false;
        well.queued = false;
        PlayMotion(well, 0, 0);
    }
}

void Step(Well &well, const float *listener) {
    const int chara = Chara(well);

    // DC_LOBBY_MODELS_MUTATE=snap (the test's twin): the camera and the heading jump, as with no ease.
    static const bool snap = TestEnvIs("DC_LOBBY_MODELS_MUTATE", "snap");
    const float       to = well.locked ? 1.0f : 0.0f;

    well.zoom = snap ? to : std::clamp(well.zoom + (well.locked ? 1.0f : -1.0f) / kEaseTicks, 0.0f, 1.0f);
    if (well.zoom_ticks >= 0 && ++well.zoom_ticks > 0 && well.zoom == to) {
        std::fprintf(stderr, "lobby-models: member %d camera %s after %d ticks\n", chara, well.locked ? "out on the whole body" : "back on the upper body",
                     well.zoom_ticks);
        well.zoom_ticks = -1;
    }

    const bool pressed = well.locked && well.attack && !well.attack_was;

    well.attack_was = well.attack;
    if (well.locked) {
        well.loop_phase = 0; // the moving shot enters again at the unlock
    }

    if (!well.locked) {
        // Back to idle facing the camera: the heading eased from where the unlock found it, the motion blended to idle.
        if (well.ease_tick >= 0) {
            const float was = well.yaw;

            well.ease_tick = snap ? (int) kEaseTicks : well.ease_tick + 1;
            well.yaw = Wrap(well.ease_from + Wrap(0.0f - well.ease_from) * Smooth(well.ease_tick / kEaseTicks));
            well.ease_step = std::max(well.ease_step, std::fabs(Wrap(well.yaw - was)));
            if (well.ease_tick >= (int) kEaseTicks) {
                std::fprintf(stderr, "lobby-models: member %d facing the camera again after %d ticks, from heading %.2f, largest step %.3f\n", chara,
                             well.ease_tick, well.ease_from, well.ease_step);
                well.ease_tick = -1;
                well.yaw = 0.0f;
            }
        }
        // The hero pose: one frame of the swing, held (flag 1), blended into from whatever played before.
        const HeroShot &hero = g_hero[chara];
        const int       asked = hero.motion >= 0 ? hero.motion : kSwing[chara][hero.stage];
        // (posed from a scene's model: the body stands idle and the pose model holds the frame, PoseFromScene)
        const int       motion = !well.posed && HasMotion(well, asked) ? asked : 0;

        if (hero.loop && motion != 0) {
            StepLoop(well.body, hero, well.loop_phase);
        } else if (motion == 0) {
            if (well.body->motion_no != 0) {
                PlayMotion(well, 0, 0);
            }
        } else {
            // (by its set: a story scene's motion keeps its time in its own set's state)
            int             index = 0;
            tagMOTION_TYPE *set = well.body->GetMotionParam(motion, &index, nullptr, nullptr, nullptr);
            const MOTION_INFO &info = set->motion_info[index];

            if (well.body->motion_no != motion) {
                PlayMotion(well, motion, 1);
            }
            well.body->motion_flags = 1;
            set->state.time = (float) info.start + hero.at * (float) (info.end - 1 - info.start);
        }
    } else if (well.attacking) {
        // DC_LOBBY_MODELS_MUTATE=nocombo (the combo twin): a press during a swing is dropped, as before.
        if (pressed && !TestEnvIs("DC_LOBBY_MODELS_MUTATE", "nocombo")) {
            well.queued = true;
        }
        StepSwing(well);
    } else if (pressed) {
        well.attacking = true;
        well.struck = false;
        well.queued = false;
        well.hits = 0;
        if (kStartCue[chara] >= 0) {
            Voice(chara, kStartCue[chara]);
        }
        if (HasMotion(well, kSwing[chara][0])) {
            StartSwing(well, 0);
        } else {
            well.stage = 0;
            well.swing_motion = 1;
            well.swing_ticks = 0;
            PlayMotion(well, 1, 4);
        }
        const MOTION_INFO &swung = well.body->motion_type.motion_info[well.swing_motion];

        std::fprintf(stderr, "lobby-models: member %d swings (motion %d, frames %d to %d)\n", chara, well.swing_motion, (int) swung.start, (int) swung.end);
    } else {
        // The run in place: the stick's heading on screen (the camera looks down -z from +z, its right is +x), the
        // motion the stick's push gives (keyCtrl, as the partner's Walk).
        const float x = well.stick_x;
        const float y = well.stick_y;

        if (x * x + y * y > 0.04f) {
            const float to = std::atan2(x, y) + well.azimuth; // up (y < 0) runs away from the camera, whichever way it stands
            well.yaw = Wrap(well.yaw + Wrap(to - well.yaw) * 0.3f);
        }
        well.body->motion_type.motion_info->speed = 0.05f;
        if (int motion = keyCtrl(x, y, well.body->motion_type.motion_info); motion != well.body->motion_no) {
            PlayMotion(well, motion, 0);
        }
    }

    Pose(well);
    Sounds(well, listener);
}

float Det3(const sceVu0FMATRIX m) {
    return m[0][0] * (m[1][1] * m[2][2] - m[2][1] * m[1][2]) - m[1][0] * (m[0][1] * m[2][2] - m[2][1] * m[0][2]) +
           m[2][0] * (m[0][1] * m[1][2] - m[1][1] * m[0][2]);
}

// World to eye, the game's way round (eye x right, y down the screen, z into it; column-major [column][row]), from eye
// looking at target. side: the handedness of the view the title draws from, so faces cull as they do there.
void LookAt(sceVu0FMATRIX view, const float *eye, const float *target, float side, float roll) {
    float f[3] = {target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]};
    float length = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);

    for (float &v : f) {
        v /= length;
    }

    // Down the screen: world down, less its part along f.
    float d[3] = {0.0f, -1.0f, 0.0f};
    float along = d[0] * f[0] + d[1] * f[1] + d[2] * f[2];

    for (int i = 0; i < 3; i++) {
        d[i] -= along * f[i];
    }
    length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    for (float &v : d) {
        v /= length;
    }

    float       r[3] = {side * (d[1] * f[2] - d[2] * f[1]), side * (d[2] * f[0] - d[0] * f[2]), side * (d[0] * f[1] - d[1] * f[0])};
    const float c = std::cos(roll);
    const float n = std::sin(roll);

    // The tilt: right and down turned about the line of sight.
    for (int i = 0; i < 3; i++) {
        const float right = r[i];
        const float down = d[i];

        r[i] = c * right + n * down;
        d[i] = c * down - n * right;
    }
    const float *axes[3] = {r, d, f};

    std::memset(view, 0, sizeof(sceVu0FMATRIX));
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            view[col][row] = axes[row][col];
        }
        view[3][row] = -(axes[row][0] * eye[0] + axes[row][1] * eye[1] + axes[row][2] * eye[2]);
    }
    view[3][3] = 1.0f;
}

} // namespace

namespace {

// The hero shot's orb (Ruby's box art): the title's own soft particle ("p01"), added onto the frame between the hands, a
// little brighter in its middle, faded with the lock as the pose is.
void DrawOrb(const Well &well, const HeroShot &hero, const sceVu0FMATRIX view, const float *eye, const gfx::LogicalRect &rect, float scale, int alpha) {
    static const char *texture_name = TestEnvOr("DC_LOBBY_MODELS_ORB_TEXTURE", "p01");
    const float        weight = 1.0f - Smooth(well.zoom);
    CFrame            *right = hero.orb > 0.0f ? well.body->frame->SearchFrame((char *) "r_hand") : nullptr;
    CFrame            *left = hero.orb > 0.0f ? well.body->frame->SearchFrame((char *) "l_hand") : nullptr;
    CTexture          *texture = TexManager.GetTexture((char *) texture_name, -1);

    if (right == nullptr || left == nullptr || texture == nullptr || weight <= 0.0f) {
        return;
    }

    sceVu0FMATRIX a, b;

    right->GetLWMatrix(a);
    left->GetLWMatrix(b);
    // (the hand frames sit at the wrists: the ball is a little on toward the fingers, ahead of her)
    const float at[3] = {0.5f * (a[3][0] + b[3][0]), 0.5f * (a[3][1] + b[3][1]), 0.5f * (a[3][2] + b[3][2]) + 0.4f};
    float       seen[3];

    for (int r = 0; r < 3; r++) {
        seen[r] = view[0][r] * at[0] + view[1][r] * at[1] + view[2][r] * at[2] + view[3][r];
    }
    if (seen[2] <= 1.0f) {
        return;
    }
    (void) eye;
    const float x = rect.x + rect.w * 0.5f + scale * seen[0] / seen[2];
    const float y = rect.y + rect.h * 0.5f + scale * seen[1] / seen[2];
    const float r = scale * hero.orb / seen[2];
    sceGsAlpha  add = {};

    add.A = 0; // source colour
    add.B = 2; // less nothing
    add.C = 0; // by the source's alpha
    add.D = 1; // onto what is there
    MGSetGsALPHA(&add);
    const CRect_i_ texel(0, 0, texture->width, texture->height);
    const int      a8 = (int) (std::clamp(alpha, 0, 0x80) * weight);

    for (int ring = 0; ring < 2; ring++) {
        const float k = ring == 0 ? 1.6f : 0.8f;

        set2DSprite(Vif1Packet, texture, CRect_i_((int) (x - r * k), (int) (y - r * k), (int) (2.0f * r * k), (int) (2.0f * r * k)), texel, 255, 248, 236,
                    (unsigned char) a8);
    }
}

} // namespace

bool Open(const int members[], int count, const int weapon_items[]) {
    Close();

    // DC_LOBBY_MODELS_HERO=<member>:<stage>,<at>,<azimuth deg>,<rise>,<roll deg>[;...] (dev looks): another hero shot.
    if (static bool read = false; !read) {
        read = true;
        for (const char *at = TestEnv("DC_LOBBY_MODELS_HERO"); at != nullptr && *at != 0;) {
            int   member = -1, stage = 0;
            float frac = 0.0f, azimuth = 0.0f, rise = 0.0f, roll = 0.0f;

            int motion = -1; // (an optional seventh: a motion by number)

            if (std::sscanf(at, "%d:%d,%f,%f,%f,%f,%d", &member, &stage, &frac, &azimuth, &rise, &roll, &motion) >= 6 && member >= 0 && member < kMembers) {
                g_hero[member] = {stage, frac, azimuth * 0.0174533f, rise, roll * 0.0174533f, motion};
                std::fprintf(stderr, "lobby-models: member %d hero shot: stage %d at %.2f, camera %.0f round, rise %.2f, roll %.0f\n", member, stage, frac,
                             azimuth, rise, roll);
            }
            at = std::strchr(at, ';');
            at = at != nullptr ? at + 1 : nullptr;
        }
        // DC_LOBBY_MODELS_LOOP=<member>:<enter>,<loop>[;...] (dev looks): the moving shot, loop after enter (-1 none).
        for (const char *at = TestEnv("DC_LOBBY_MODELS_LOOP"); at != nullptr && *at != 0;) {
            int member = -1, enter = -1, loop = -1;

            if (std::sscanf(at, "%d:%d,%d", &member, &enter, &loop) == 3 && member >= 0 && member < kMembers && loop >= 0) {
                g_hero[member].loop = true;
                g_hero[member].enter = enter;
                g_hero[member].motion = loop;
                std::fprintf(stderr, "lobby-models: member %d moving shot: motion %d after %d\n", member, loop, enter);
            }
            at = std::strchr(at, ';');
            at = at != nullptr ? at + 1 : nullptr;
        }
        // DC_LOBBY_MODELS_LENS=<member>:<scale>[;...] (dev looks): the hero shot's own lens (HeroShot::lens).
        for (const char *at = TestEnv("DC_LOBBY_MODELS_LENS"); at != nullptr && *at != 0;) {
            int   member = -1;
            float lens = 0.0f;

            if (std::sscanf(at, "%d:%f", &member, &lens) == 2 && member >= 0 && member < kMembers) {
                g_hero[member].lens = lens;
                std::fprintf(stderr, "lobby-models: member %d lens %.0f\n", member, lens);
            }
            at = std::strchr(at, ';');
            at = at != nullptr ? at + 1 : nullptr;
        }
        // DC_LOBBY_MODELS_EYE=<member>:<frame>,<dx>,<dy>,<dz>,<lx>,<ly>,<lz>[;...] (dev looks): the camera placed outright
        // (HeroShot::eye_frame, eye_off, eye_look).
        static char eye_frames[kMembers][32];

        for (const char *at = TestEnv("DC_LOBBY_MODELS_EYE"); at != nullptr && *at != 0;) {
            int   member = -1;
            char  frame[32];
            float v[6];

            if (std::sscanf(at, "%d:%31[^,],%f,%f,%f,%f,%f,%f", &member, frame, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 8 && member >= 0 &&
                member < kMembers) {
                std::strcpy(eye_frames[member], frame);
                g_hero[member].eye_frame = eye_frames[member];
                std::copy(v, v + 3, g_hero[member].eye_off);
                std::copy(v + 3, v + 6, g_hero[member].eye_look);
                std::fprintf(stderr, "lobby-models: member %d eye at %s + (%.2f %.2f %.2f) looking (%.3f %.3f %.3f)\n", member, frame, v[0], v[1], v[2], v[3],
                             v[4], v[5]);
            }
            at = std::strchr(at, ';');
            at = at != nullptr ? at + 1 : nullptr;
        }
        // DC_LOBBY_MODELS_MOVES=<member>=<file>,<configuration>[;...] (dev looks): a story scene's motions on the body.
        static char moves[kMembers][128], moves_cfg[kMembers][64];

        for (const char *at = TestEnv("DC_LOBBY_MODELS_MOVES"); at != nullptr && *at != 0;) {
            int  member = -1;
            char file[128], cfg[64];

            if (std::sscanf(at, "%d=%127[^,;],%63[^;]", &member, file, cfg) == 3 && member >= 0 && member < kMembers) {
                std::strcpy(moves[member], file);
                std::strcpy(moves_cfg[member], cfg);
                g_hero[member].moves = moves[member];
                g_hero[member].moves_cfg = moves_cfg[member];
            }
            at = std::strchr(at, ';');
            at = at != nullptr ? at + 1 : nullptr;
        }
    }

    if (g_lobby_models.pack == nullptr) {
        g_lobby_models.pack = reinterpret_cast<u_int *>(new (std::align_val_t{64}) u_char[kPackBytes]);
    }

    int  loaded = 0;
    char list[64] = "";

    for (int i = 0; i < count && i < kMembers; i++) {
        const int chara = members[i];

        if (chara < 0 || chara >= kMembers || g_lobby_models.wells[chara].loaded) {
            std::fprintf(stderr, "lobby-models: member %d left out (out of range or twice)\n", chara);
            continue;
        }
        if (Load(chara, weapon_items != nullptr ? &weapon_items[i] : nullptr)) {
            loaded++;
            std::snprintf(list + std::strlen(list), sizeof list - std::strlen(list), "%s%d", loaded > 1 ? " " : "", chara);
        }
    }

    int used = 0;

    for (const CTexture &texture : TexManager.textures) {
        used += texture.name[0] != 0 ? 1 : 0;
        // (dev looks: the textures there are to draw with, by name and block)
        if (static const bool list_textures = TestEnvOn("DC_LOBBY_MODELS_TEXTURES"); list_textures && texture.name[0] != 0) {
            std::fprintf(stderr, "lobby-models: texture %s block %d\n", texture.name, texture.block);
        }
    }
    g_lobby_models.open = loaded;
    std::fprintf(stderr, "lobby-models: open %d of %d members (%s), texture slots %d of 196, staging %d of %d quadwords\n", loaded,
                 count, list, used, TexManager.buffer_used, TexManager.buffer_size);
    return loaded > 0;
}

void Close() {
    int closed = 0;

    for (int chara = 0; chara < kMembers; chara++) {
        Well &well = g_lobby_models.wells[chara];

        if (!well.loaded) {
            continue;
        }
        EmptyBlock(kBlocks[chara]);
        if (g_hero[chara].show && well.posed) {
            EmptyBlock(ShowBlock(chara));
            std::memset(well.pose_model, 0, static_cast<size_t>(well.pose_arena.used) * 16);
        }
        // The renderer keeps a built model's record until its block loses its tag: zeroed, the records go at the next
        // frame's sweep; freed, the sweep would read released pages.
        std::memset(well.model, 0, static_cast<size_t>(well.arena.used) * 16);
        well = Well{well.body, well.weapon, well.tex_anime, well.model, well.arena, well.pose, well.pose_model, well.pose_anime, well.pose_arena};
        closed++;
    }

    // The partner's voice port held the lobby's last set: the partner's own again, for its next cue.
    if (g_lobby_models.voice_set >= 0 && g_partner.loaded_chara >= 0 && g_partner.loaded_chara != g_lobby_models.voice_set) {
        SoundCoopLoadVoice(g_partner.loaded_chara);
    }
    g_lobby_models.voice_set = -1;

    if (closed > 0 || g_lobby_models.open > 0) {
        std::fprintf(stderr, "lobby-models: closed %d models\n", closed);
    }
    g_lobby_models.open = 0;
}

bool Loaded(int member) {
    return Find(member) != nullptr;
}

void SetLocked(int member, bool locked) {
    Well *well = Find(member);

    if (well == nullptr || well->locked == locked) {
        return;
    }

    well->locked = locked;
    well->stick_x = well->stick_y = 0.0f;
    well->attack = well->attack_was = false;
    well->zoom_ticks = 0;
    if (locked) {
        well->ease_tick = -1;
    } else {
        well->attacking = false;
        well->ease_from = well->yaw;
        well->ease_tick = 0;
        well->ease_step = 0.0f;
    }
    std::fprintf(stderr, "lobby-models: member %d %s (heading %.2f)\n", member, locked ? "locked: whole body, driven" : "unlocked: easing back to idle", well->yaw);
}

void SetSilhouette(int member, bool silhouette) {
    Well *well = Find(member);

    if (well == nullptr || well->silhouette == silhouette) {
        return;
    }
    well->silhouette = silhouette;
    std::fprintf(stderr, "lobby-models: member %d %s\n", member, silhouette ? "a silhouette (not yet in the story)" : "shown in colour");
}

void SetShade(int member, float shade) {
    Well *well = Find(member);

    shade = std::clamp(shade, 0.0f, 1.0f);
    if (well == nullptr || well->shade == shade) {
        return;
    }
    well->shade = shade;
}

void Drive(int member, float stick_x, float stick_y, bool attack) {
    Well *well = Find(member);

    if (well == nullptr || !well->locked) {
        return;
    }
    well->stick_x = std::clamp(stick_x, -1.0f, 1.0f);
    well->stick_y = std::clamp(stick_y, -1.0f, 1.0f);
    well->attack = attack;
}

void Draw(int member, float x, float y, float w, float h, int alpha, bool step) {
    Well *found = Find(member);

    if (found == nullptr || w < 4.0f || h < 4.0f) {
        return;
    }

    Well &well = *found;

    // The rect in the renderer's logical frame (the title's 448 rows are centred in it, as its MENU_Y lowers them).
    const gfx::LogicalRect rect = {x, y + (SCREEN_HEIGHT - 448) / 2.0f, w, h};
    const RenderInfo       saved_info = mgRenderInfo;
    const dc::render::MGPortRegisters  saved_registers = dc::render::MGPortCurrent();
    const float            side = Det3(saved_info.view) < 0.0f ? -1.0f : 1.0f;
    const float            title_scale = saved_info.scale[0] > 1.0f ? saved_info.scale[0] : 512.0f;

    // The two shots, eased: the upper body (head and chest fill the well; Jep, 2026-10-31: not dead on but about 45 degrees round,
    // the eye a little below looking up) and the whole body from the front with room for a swing, the eye a little above. The
    // distance that fits each in the well's height and width.
    const float s = Smooth(well.zoom);
    const float tall = well.height;
    const float lens = g_hero[member].lens;
    const float scale = lens > 0.0f ? lens + (title_scale - lens) * s : title_scale;
    // The hero shot frames the pose as it is held (a swing leans, lunges and reaches): its frames' extent, crown down to
    // about the knees, centred on it. The locked shot: crown to feet about the model's place, room for a swing.
    // (the card is tall and narrow: the pose fills its height, and a reaching arm or weapon may run off its sides)
    // DC_LOBBY_MODELS_BUST=1 (Jep, 2026-11-01, a step at a time: a zoomed bust shot): head and shoulders fill the card.
    static const bool bust = TestEnvOn("DC_LOBBY_MODELS_BUST");
    float hero_half = bust ? 0.17f * tall : std::max(1.15f * well.reach_up + 0.04f * tall, well.top - well.centre[1] + 0.06f * tall);
    float hero_wide = bust ? 0.0f : 0.80f * well.reach_side + 0.04f * tall;
    float hero_y = bust ? well.top + 0.07f * tall - hero_half : well.centre[1]; // (hair stands above the highest frame)
    float centre_x = well.centre[0], centre_z = well.centre[2];
    // The hero's own shot (HeroShot::shot_frame): centred on that frame of the model the hero shot shows.
    if (const HeroShot &shot = g_hero[member]; shot.shot_frame != nullptr) {
        CCharacter *shown = shot.show && well.posed ? well.pose : well.body;

        if (CFrame *frame = shown->frame->SearchFrame(const_cast<char *>(shot.shot_frame)); frame != nullptr) {
            sceVu0FMATRIX world;

            frame->GetLWMatrix(world);
            hero_half = shot.shot_half * tall;
            hero_wide = 0.0f;
            hero_y = world[3][1] + shot.shot_up * tall;
            // (across the ground eased over to the body's own place as the lock's camera moves out: the shown model stands
            // still, the body runs)
            centre_x = world[3][0] + (well.centre[0] - world[3][0]) * s;
            centre_z = world[3][2] + (well.centre[2] - world[3][2]) * s;
        }
    }
    // DC_LOBBY_MODELS_SHOT=<member>:<frame>,<half>,<up>[;...] (dev looks, Jep 2026-11-01's two card pictures: waist up, whole
    // body): the shot centred on that frame, <up> x the height above it, <half> x the height from the centre to the card's top.
    // (a scene model's helper frames pull the frames' mean off the body)
    for (const char *at = TestEnv("DC_LOBBY_MODELS_SHOT"); at != nullptr && *at != 0;) {
        int   shot_member = -1;
        char  name[32] = "";
        float shot_half = 0.0f, up = 0.0f;

        if (std::sscanf(at, "%d:%31[^,],%f,%f", &shot_member, name, &shot_half, &up) == 4 && shot_member == member) {
            if (CFrame *frame = well.body->frame->SearchFrame(name); frame != nullptr) {
                sceVu0FMATRIX world;

                frame->GetLWMatrix(world);
                hero_half = shot_half * tall;
                hero_wide = 0.0f;
                hero_y = world[3][1] + up * tall;
                centre_x = world[3][0];
                centre_z = world[3][2];
            }
        }
        at = std::strchr(at, ';');
        at = at != nullptr ? at + 1 : nullptr;
    }
    const float half = hero_half + (0.56f * tall - hero_half) * s; // (locked: crown to feet, room for a swing)
    const float wide = hero_wide + (0.30f * tall - hero_wide) * s;
    const float distance = std::max(scale * half / (0.5f * h), scale * wide / (0.5f * w));
    // Across the ground the camera follows the pose, eased (a swing's lunge carries the body forward, idle or locked).
    if (!well.followed) {
        well.followed = true;
        well.follow[0] = centre_x;
        well.follow[1] = centre_z;
    }
    if (step) {
        well.follow[0] += (centre_x - well.follow[0]) * 0.15f;
        well.follow[1] += (centre_z - well.follow[1]) * 0.15f;
    }
    float           target[3] = {well.follow[0], hero_y + (0.52f * tall - hero_y) * s, well.follow[1]};
    const HeroShot &hero = g_hero[member];
    const float     azimuth = hero.azimuth * (1.0f - s);
    const float     rise = hero.rise * (1.0f - s) + 0.06f * s;
    float           eye[4] = {target[0] + distance * std::sin(azimuth), target[1] + rise * distance, target[2] + distance * std::cos(azimuth), 1.0f};

    // The camera placed outright (HeroShot::eye_frame), eased over to the one above with the lock. Logged once a member.
    // DC_LOBBY_MODELS_MUTATE=eye_unplaced (l-lobby -Case slots' twin): the camera from azimuth and rise, as before.
    static const bool eye_unplaced = TestEnvIs("DC_LOBBY_MODELS_MUTATE", "eye_unplaced");
    // (on the model the hero shot shows: a scene model shown whole stands where its hip meets the body's)
    CCharacter *framed = hero.show && well.posed ? well.pose : well.body;
    if (CFrame *frame = hero.eye_frame != nullptr && !eye_unplaced ? framed->frame->SearchFrame(const_cast<char *>(hero.eye_frame)) : nullptr; frame != nullptr) {
        sceVu0FMATRIX world;

        frame->GetLWMatrix(world);
        if (static unsigned placed_said = 0; (placed_said & (1u << member)) == 0) {
            placed_said |= 1u << member;
            std::fprintf(stderr, "lobby-models: member %d eye placed from %s (%.2f %.2f %.2f), looking (%.2f %.2f %.2f)\n", member, hero.eye_frame, hero.eye_off[0],
                         hero.eye_off[1], hero.eye_off[2], hero.eye_look[0], hero.eye_look[1], hero.eye_look[2]);
        }
        for (int i = 0; i < 3; i++) {
            const float placed = world[3][i] + hero.eye_off[i];

            eye[i] = placed + (eye[i] - placed) * s;
            target[i] = placed + hero.eye_look[i] * 10.0f + (target[i] - placed - hero.eye_look[i] * 10.0f) * s;
        }
    }

    well.azimuth = azimuth;
    if (static const bool trace = TestEnvOn("DC_LOBBY_MODELS_TRACE"); trace && member == 3 && s > 0.0f && s < 1.0f) {
        std::fprintf(stderr, "lobby-models: trace s %.2f target (%.1f %.1f %.1f) eye (%.1f %.1f %.1f) dist %.1f half %.1f centre (%.1f %.1f %.1f)\n", s,
                     target[0], target[1], target[2], eye[0], eye[1], eye[2], distance, half, well.centre[0], well.centre[1], well.centre[2]);
    }

    // (a second draw of the member this frame, in another player's slot: drawn as it stands, not stepped again)
    if (step) {
        Step(well, eye);
    }

    // The first draw of each model, and each lock's first: the shot it is framed in, for a look at the log.
    if (static unsigned said = 0; (said & (1u << member)) == 0) {
        said |= 1u << member;
        std::fprintf(stderr, "lobby-models: member %d drawn in (%.0f %.0f %.0f %.0f): projection %.1f, height %.1f, eye %.1f at %.1f, near %.1f\n", member,
                     rect.x, rect.y, rect.w, rect.h, scale, tall, distance, eye[1], saved_info.near[2]);
    }

    sceVu0FMATRIX view;

    LookAt(view, eye, target, side, hero.roll * (1.0f - s));
    dc::render::MGPortRestoreRegisters(); // depth tested and written: the title's 2D turned the test off
    dc::render::MGPortCurrent().window = rect;
    const uint8_t black[4] = {0, 0, 0, 0};
    gfx::Clear(false, black, true, 0.0f, &rect); // the backdrop's depth out of the well
    mgRenderInfo.offset[0] = 2048.0f - dc::render::kScreenWidth * 0.5f + rect.x + rect.w * 0.5f;
    mgRenderInfo.offset[1] = 2048.0f - dc::render::kScreenHeight * 0.5f + rect.y + rect.h * 0.5f;
    mgRenderInfo.projection = mgRenderInfo.scale[0] = mgRenderInfo.scale[1] = scale; // (the hero's lens; back with saved_info)
    MGPortSetViewMatrixNoCut(view, const_cast<float *>(eye));

    sceVu0FVECTOR light0 = {2.4578f, 9.9294f, -2.8074f, 0.0f}; // the title's own directions
    sceVu0FVECTOR light1 = {4.6086f, -10.4028f, -0.8286f, 0.0f};
    sceVu0FVECTOR light2 = {0.0f, 0.0f, -10.0f, 0.0f};
    sceVu0FMATRIX light;
    sceVu0FMATRIX colour = {
        {120.0f, 116.0f, 108.0f, 0.0f},
        {40.0f,  44.0f,  56.0f,  0.0f},
        {48.0f,  48.0f,  48.0f,  0.0f},
        {0.0f,   0.0f,   0.0f,   0.0f},
    };
    sceVu0FVECTOR ambient = {48.0f, 48.0f, 48.0f, (float) std::clamp(alpha, 0, 0x80)};

    // A silhouette: no light at all, so the body and its weapon draw black at the well's fade.
    if (well.silhouette) {
        std::memset(colour, 0, sizeof colour);
        ambient[0] = ambient[1] = ambient[2] = 0.0f;
    }
    // Darkened (an unselected card): every light scaled down, the fade (ambient's alpha) kept.
    for (int c = 0; c < 3; c++) {
        for (int r = 0; r < 3; r++) {
            colour[c][r] *= well.shade;
        }
        ambient[c] *= well.shade;
    }

    sceVu0Normalize(light0, light0);
    sceVu0Normalize(light1, light1);
    sceVu0Normalize(light2, light2);
    sceVu0NormalLightMatrix(light, light0, light1, light2);
    MGSetPLight(light, colour);
    MGSetAmbient(ambient);

    // Shown whole (HeroShot::show): the scene model and the body crossed over the lock's camera move and back over the
    // unlock's, each whole across the middle of it (two half-faded models read as an empty well).
    const bool  shows = hero.show && well.posed;
    const float fade = ambient[3];

    if (shows && s < 1.0f) {
        ambient[3] = fade * std::min(1.0f, 2.0f * (1.0f - s));
        MGSetAmbient(ambient);
        TexManager.ReloadTexture(Vif1Packet, ShowBlock(member));
        well.pose->TextureAnime(ShowBlock(member));
        well.pose->Draw();
    }
    if (!shows || s > 0.0f) {
        ambient[3] = shows ? fade * std::min(1.0f, 2.0f * s) : fade;
        MGSetAmbient(ambient);
        TexManager.ReloadTexture(Vif1Packet, kBlocks[member]);
        well.body->TextureAnime(kBlocks[member]);
        if (well.posed && hero.face >= 0 && s < 0.5f && !shows) {
            well.pose->TextureAnime(kBlocks[member]); // the scene's face (a wink) while the hero shot shows
        }
        well.body->Draw();
        if (well.weapon_loaded) {
            if (step) {
                well.weapon->Step(); // follows the hand the body's Step posed
            }
            well.weapon->Draw();
        }
    }
    DrawOrb(well, hero, view, eye, rect, scale, alpha);

    MGPortSetViewMatrixNoCut(const_cast<RenderInfo &>(saved_info).view, const_cast<RenderInfo &>(saved_info).view_position);
    mgRenderInfo = saved_info;
    dc::render::MGPortCurrent() = saved_registers;
}

// ---- The dev hook ------------------------------------------------------------------------------------------------------

namespace {

// The demo's wells: the lobby's own (coop-lobby: 84-wide columns, 68 x 150 wells), six across.
constexpr int kDemoW = 68, kDemoH = 150, kDemoPitch = 96, kDemoLeft = 46;

struct Demo {
    bool opened = false;
    int  tick = 0;
} g_lobby_models_demo;

} // namespace

// DC_LOBBY_MODELS_SHEET=<member>:<dir> (dev looks: finding a pose, Jep 2026-10-31): one member's every motion, a page each,
// the same model drawn in six wells at 0, 20 ... 100% of it, 24 ticks a page in the log's order (<dir> is only logged).
bool SheetDraw(int fade) {
    static const char *spec = TestEnv("DC_LOBBY_MODELS_SHEET");
    static int         member = -1, motion = -1, tick = 0;
    static char        dir[400];

    if (spec == nullptr) {
        return false;
    }
    if (member < 0) {
        if (std::sscanf(spec, "%d:%399[^\n]", &member, dir) != 2 || member < 0 || member >= kMembers) {
            spec = nullptr;
            return false;
        }
        Open(&member, 1);
        std::fprintf(stderr, "lobby-models: sheet of member %d to %s\n", member, dir);
    }

    Well *well = Find(member);

    if (well == nullptr) {
        return true;
    }
    // A page: 24 ticks (the motion blended in), its frame taken on the 22nd (the frame before is whole).
    if (motion < 0 || ++tick >= 24) {
        tick = 0;
        do {
            motion++;
        } while (motion < 96 && !HasMotion(*well, motion));
        if (motion >= 96) {
            std::fprintf(stderr, "lobby-models: sheet done\n");
            spec = nullptr;
            return true;
        }
        const MOTION_INFO &info = well->body->motion_type.motion_info[motion];
        std::fprintf(stderr, "lobby-models: sheet motion %d: frames %d to %d\n", motion, info.start, info.end);
        // The frames: netplay's series (DC_COOP_TEST_SHOT_SERIES=<dir>:22:<to>:24, _MARK=1) counted from here, one a page on
        // its 22nd tick (a frame cannot be read back from inside the draw).
        if (static bool marked = false; !marked) {
            marked = true;
            NetplayTestSeriesMark();
        }
    }
    for (int cell = 0; cell < 6; cell++) {
        g_hero[member].motion = motion;
        g_hero[member].at = (float) cell / 5.0f;
        g_hero[member].aims = nullptr; // (the motions as they are)
        g_hero[member].orb = 0.0f;
        well->followed = false; // each cell framed on its own pose at once
        Draw(member, (float) (kDemoLeft + kDemoPitch * cell), 40.0f, (float) kDemoW, (float) kDemoH, fade);
    }
    return true;
}

void DemoDraw(int fade) {
    static const char *mode = TestEnv("DC_LOBBY_MODELS_DEMO");

    if (SheetDraw(fade)) {
        return;
    }
    if (mode == nullptr || mode[0] == '0') {
        return;
    }

    Demo &demo = g_lobby_models_demo;

    if (!demo.opened) {
        static const int members[kMembers] = {0, 1, 2, 3, 4, 5};

        demo.opened = true;
        demo.tick = 0;
        Open(members, kMembers);
        // =well: as the lobby on a fresh save, Goro on not yet reached (silhouettes).
        for (int member = 2; member < kMembers && std::strcmp(mode, "well") == 0; member++) {
            SetSilhouette(member, true);
        }
        // DC_LOBBY_MODELS_DIM=<shade> (dev looks): every member but Ruby (3) drawn at that brightness, as unselected cards.
        if (const char *shade = TestEnv("DC_LOBBY_MODELS_DIM"); shade != nullptr) {
            for (int member = 0; member < kMembers; member++) {
                SetShade(member, member == 3 ? 1.0f : static_cast<float>(std::atof(shade)));
            }
        }
        std::fprintf(stderr, "lobby-models: demo (%s) on the title menu\n", mode);
        NetplayTestSeriesMark(); // (dev looks: DC_COOP_TEST_SHOT_SERIES with _MARK=1 counts the demo's ticks from here)
    }

    // Each well in turn, six seconds each: locked, run round a circle with three swings, unlocked at 4.5 s.
    if (std::strcmp(mode, "idle") != 0 && std::strcmp(mode, "lobby") != 0) { // (lobby: idle too)
        const int member = (demo.tick / 360) % kMembers;
        const int t = demo.tick % 360;

        if (t == 0) {
            SetLocked(member, true);
        } else if (t == 270) {
            SetLocked(member, false);
        } else if (t > 30 && t < 270) {
            const float angle = (float) (t - 30) * 0.03f;
            // a string of four presses 12 ticks apart (the combo), then single swings
            const bool  swing = (t >= 120 && t < 168 && (t - 120) % 12 < 4) || (t >= 190 && t < 194) || (t >= 236 && t < 240);
            const bool  still = t >= 200 && t < 230; // a moment at rest: the run blends back to idle

            Drive(member, still || swing ? 0.0f : std::sin(angle), still || swing ? 0.0f : -std::cos(angle), swing);
        }
    }
    demo.tick++;

    // DC_LOBBY_MODELS_FACE_SCAN=<member> (dev looks: finding a wink on a face sheet): its face animation's first record held
    // and moved over the sheet's eye strips, 128x50 each (4 across from x 0, 7 down from y 34), one a page of 24 ticks.
    // DC_LOBBY_MODELS_FACE_EYES=<member>:<x>,<y>: held on that one strip (e228c05a's wink: 128,34).
    static int eyes_member = -1, eyes_x = 0, eyes_y = 0;

    if (static bool read = false; !read) {
        read = true;
        if (const char *eyes = TestEnv("DC_LOBBY_MODELS_FACE_EYES"); eyes != nullptr && std::sscanf(eyes, "%d:%d,%d", &eyes_member, &eyes_x, &eyes_y) != 3) {
            eyes_member = -1;
        }
    }
    if (static const int scan = TestEnvInt("DC_LOBBY_MODELS_FACE_SCAN", eyes_member); scan >= 0) {
        const int face = TestEnvInt("DC_LOBBY_MODELS_FACE", g_hero[scan].face);

        if (Well *well = Find(scan); well != nullptr && face >= 0) {
            if (CTexAnimeData *record = well->body->tex_anime.first[face]; record != nullptr) {
                const int page = std::min(demo.tick / 24, 27);

                record->duration = -1;
                record->next = nullptr;
                record->source_x = (short) (eyes_member >= 0 ? eyes_x : 128 * (page % 4));
                record->source_y = (short) (eyes_member >= 0 ? eyes_y : 34 + 50 * (page / 4));
                well->body->tex_anime.current[face] = record;
                if (demo.tick % 24 == 0) {
                    std::fprintf(stderr, "lobby-models: face scan page %d: eyes from (%d %d)\n", page, record->source_x, record->source_y);
                }
            }
        }
    }

    // DC_LOBBY_MODELS_DEMO=lobby: the wells exactly where coop-lobby's cards put them (columns 88 apart from 60, each
    // well 8 in and 30 down a board at 136 in the 480-row frame), lit as the lobby lights them, for a look inside its frame.
    static const bool lobby = std::strcmp(mode, "lobby") == 0;
    const int         left = lobby ? 68 : kDemoLeft, pitch = lobby ? 88 : kDemoPitch, top = lobby ? 150 : 40;

    for (int member = 0; member < kMembers; member++) {
        // DC_LOBBY_MODELS_DEMO=well (and lobby): the lobby's lit well behind each (coop-lobby's gradient), drawn first.
        if (static const bool well = std::strcmp(mode, "well") == 0 || lobby; well) {
            spRGBA top_colour = {58, 70, 96, (unsigned char) std::clamp(fade, 0, 128)};
            spRGBA bottom_colour = {14, 17, 28, (unsigned char) std::clamp(fade, 0, 128)};

            set2DSpriteC4(Vif1Packet, CRect_i_(left + pitch * member, top + (SCREEN_HEIGHT - 448) / 2, kDemoW, kDemoH), &top_colour, &top_colour,
                          &bottom_colour, &bottom_colour);
        }
        Draw(member, (float) (left + pitch * member), (float) top, (float) kDemoW, (float) kDemoH, fade);
    }
}

void DemoClose() {
    if (g_lobby_models_demo.opened) {
        g_lobby_models_demo.opened = false;
        Close();
    }
}

} // namespace coop::lobbymodels
