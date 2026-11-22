#include "coop/game_ns.hpp"
#include "coop/coop_audio.hpp"
#include "coop/coop_present.hpp" // coop-router
#include "coop/coop_party.hpp"   // coop-entrance2: PlayerDrawnAt
#include "coop/coop_actor.hpp"   // coop-farcull: ActorLocalIndex
#include "snd.hpp"              // coop-pt1109c: SndBgmLoad, SndBgmPlay, SndBgmStop

namespace dc::game {
enum class BgmPlayState : int; // repin: snd.cpp's (Stopped 0, Playing 1, Paused 2), out of its anonymous namespace (recipe 0052)
extern int          now_bgm_no; // coop-pt1109c: ps2/src/snd.cpp
extern BgmPlayState now_bgm_play;
extern int          now_bgm_vol;
extern float        now_bgm_fade_vol;
} // namespace dc::game

#include <libvu0.h>

#include <algorithm>
#include <array> // repin: snd.cpp's tables are std::arrays now
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>

#include "boxvu0.hpp"
#include "btmisc.hpp"
#include "camera.hpp"
#include "camerafollow.hpp"
#include "character.hpp"
#include "dun/gameloop.hpp"
#include "itemdata.hpp" // coop-comboaudio: CHARA_*
#include "menu_draw.hpp"
#include "monstorunit.hpp"
#include "shot_freefuncs.hpp"
#include "snd.hpp"
#include "sound.hpp"
#include "sound_effect.h"
#include "userstatus.hpp"

#include "coop/coop.hpp"
#include "coop/coop_camera.hpp"
#include "coop/coop_combat.hpp" // coop-comboaudio: PartnerMeleeKey
#include "coop/coop_core.hpp"
#include "coop/coop_fullmenu.hpp"
#include "coop/coop_town.hpp" // coop-soundpt: CoopTownListener
#include "coop/coop_worlds.hpp" // coop-onesound: CoopWorldsApart
#include "coop/netplay.hpp"

// snd.cpp's state that picks a sound's table row (GetSeInfo).
namespace dc::game {
extern int now_voice_set;
extern int se_table_no;
extern int basic_se_table_no;
extern sceVu0FVECTOR camera_pos;
extern sceVu0FVECTOR camera_dir;
// repin: snd.cpp's tables, out of its anonymous namespace (recipe 0052).
extern SND_SE_INFO                     se_info[2801];
extern SND_SE_INFO                     geo[199];
extern SND_SE_INFO                     dun[199];
extern std::array<SND_SE_INFO *, 101>  cap_se_info;
extern std::array<SND_SE_INFO *, 11>   voice_info;
extern SND_SE_INFO                     special_se_info[65];
void SndSePlay(int se_no, float volume, float pan, int voice); // repin: snd.cpp's, out of its anonymous namespace (recipe 0052)
} // namespace dc::game

// btmisc.cpp's crossfade state, which retail steps as it always does.
namespace dc::game {
extern bool  battle_music_active; // was BtBattleMusic_Flag (an int) at 6ec3b808
extern int   battle_music_wait;   // was BtBattleMusic_Wait
extern float battle_music_volume; // was BtBattleMusic_Vol
void         BtBattleMusic_Excg(float distance, float *field_volume, float *battle_volume);

extern const std::array<s32, 6> defWeapon__6; // game/dun/gameloop.cpp: each member's starter weapon (no warning for it, as motionDrive)
extern int BtEventMode;
extern s32 mode;
extern s32 gameTask; // ps2/src/dun/gameloop.cpp
extern CCharacter *Chara; // editloop.cpp: the town's player
} // namespace dc::game

namespace coop {
bool PresentEngine(); // coop_opener.hpp (not included: its PlayerPresenting(int) would meet this file's own)
// coop-onesound (below): whether this copy leaves the shared sounds to player 1's window on this PC, and its ambient port volume.
bool SharedSoundsElsewhere();
int  AmbientPortVolume(int volume);
bool SharedLoopSe(int se_no);
bool SharedFlatSe(int se_no);
}

namespace coop {

// coop-comboaudio (the combo rows): the key sounds each player's attack plays, in order (KeySoundRecord: the player's through
// SndSePlay, hers through PartnerSound). Read by the test bot only; nothing in the tick reads it.
namespace {
struct KeySounds {
    int list[2][64] = {};
    int count[2] = {};
} g_key_sounds;

bool KeySound(int se_no) {
    return (se_no >= SE_CHARA_ACTION && se_no <= SE_CHARA_ACTION_4) || (se_no >= SE_CHARA_SHOUT && se_no <= SE_CHARA_SHOUT_4) || se_no == SE_GORO_SMASH;
}
} // namespace

void KeySoundRecord(int player, int se_no) {
    if (player < 1 || player > 2 || !KeySound(se_no) || g_key_sounds.count[player - 1] >= 64) {
        return;
    }
    g_key_sounds.list[player - 1][g_key_sounds.count[player - 1]++] = se_no;
}

void KeySoundsClear(int player) {
    g_key_sounds.count[player - 1] = 0;
}

int KeySoundsRead(int player, const int **list) {
    *list = g_key_sounds.list[player - 1];
    return g_key_sounds.count[player - 1];
}

namespace {

// Where a placed sound is heard at full volume and where it fades out: the player's footsteps'
// (SndPlayFootSound), as the partner's own sounds are (coop_combat.cpp).
constexpr float kNear = 50.0f;
constexpr float kFar = 300.0f;

// shot_freefuncs.cpp's floors of each dungeon: the last floor has no battle music.
constexpr int kMaxFloor[7] = {15, 17, 18, 18, 15, 25, 100};

// The ambient port's battle sequence (SetBattleStyle's SndAmbientPlay(0)).
constexpr int kBattleSequence = 0;

// Steps without a SetBattleStyle before this copy's own battle music lets go (a floor change, the
// town); a pause menu is shorter.
constexpr int kMusicIdleSteps = 600;

bool Logging() {
    COOP_TEST_SWITCH(bool, on, TestEnv("DC_COOP_AUDIO_LOG") != nullptr); // (coop-warmconvert WS7 round 4: a job switch)
    return on;
}

// The tests' mutation twins (DC_COOP_AUDIO_MUTATE): each breaks one feature, and its test must fail.
//   listener  every copy listens from the player's camera
//   music     every copy's battle music follows the player's nearest monster, as the game's does
//   voice     the partner speaks from the game's voice set (the player's)
//   flat      the player's own sounds play flat on the partner's copy, as retail plays them
//   bgm       once this copy's battle music has played, its field music stays down at the battle mix
//   menuse    a menu's sounds play on both linked copies, not only its owner's (coop-menuse)
//   chestflat the player's chest, Atla and gate key presentations play flat on the partner's copy, as retail plays them
//             (coop-chestsound)
// The value is copied: the environment may move once something else sets a variable.
// coop-warmconvert (WS7 round 4): read again at each job on a warm pair (a twin's mutation as its job's own,
// lib\ServePool.ps1 ServeJobMutations; DC_COOP_MUTATE=switch_stale: once a process).
// coop-pt1109c (Jep's 11-09 playtest 1o: she joined P1 in B5's opening and the scene had no music): the state set carries the game's words for the music (now_bgm_no,
// now_bgm_play, snd.cpp), not the sound driver's own track, which is this copy's. A join into a running scene left the words saying the scene's track played while the
// driver had whatever this copy last loaded, stopped by the zone's load, and the scene's start had run before the join, so nothing ever started it; the next SndBgmLoad of
// that number returns at once, as the word already says it. After an apply the driver is made to match the words: a track this copy hasn't got is loaded and started, a
// track this copy already plays goes on undisturbed. DC_COOP_MUTATE=no_bgm_carry (twin): as before. Open: SndBgmPlay's sequence number isn't in the set (retail
// keeps no word for it); the track starts at sequence 0, as all but the scripts' own _BGM_PLAY calls have it.
int g_bgm_driver_no = -1;
int g_bgm_driver_play = 0;

bool AudioMutated(const char *what) {
    COOP_TEST_SWITCH(std::string, value, TestEnvOr("DC_COOP_AUDIO_MUTATE", ""));
    return value == what;
}

// coop-menuse: whose the sounds played now are (SoundOwner).
struct Owner {
    int  player = 0;
    bool menu_only = false;
} g_owner;

// The menu's own sounds (ComMenuSePlay's) and the lock-on's.
bool MenuSound(int se_no) {
    return (se_no >= MENU_SOUND_CURSOR && se_no <= MENU_SOUND_REFUSE) || se_no == SE_LOCK_ON;
}

// Whether the sound belongs on the other player's copy, not this one. Logged (DC_COOP_AUDIO_LOG) for every owned
// sound and for every menu sound nobody owns while linked, so a test reads each copy's.
bool OthersSound(int se_no) {
    const int  local = NetplayLocalPlayer();
    const bool owned = g_owner.player != 0 && (!g_owner.menu_only || MenuSound(se_no));

    if (!owned) {
        if (Logging() && local != 0 && MenuSound(se_no)) {
            std::printf("coop-audio: menu sound %d of nobody played on player %d's copy\n", se_no, local);
        }
        return false;
    }

    const bool others = local != 0 && local != g_owner.player && !AudioMutated("menuse");

    if (Logging()) {
        std::printf("coop-audio: menu sound %d of player %d %s on player %d's copy\n", se_no, g_owner.player,
                    others ? "left out" : "played", local);
    }
    return others;
}

// Whether the sound is one player's alone now (a SoundOwner's), wherever it is heard.
bool OwnedNow(int se_no) {
    return g_owner.player != 0 && (!g_owner.menu_only || MenuSound(se_no));
}

bool PartnerOnFloor() {
    return Enabled() && g_partner.visible && g_partner.chara != nullptr;
}

// This copy hears from the partner: player 2's copy while the cameras are independent.
bool PartnerListens() {
    return PartnerOnFloor() && NetplayLocalPlayer() == 2 && IndependentCameras();
}

// ---------------------------------------------------------------------------------------------
// Sound rows, as snd.cpp's GetSeInfo and GetPortNo pick them.

SND_SE_INFO *SeInfo(int se_no) {
    static SND_SE_INFO *const basic[2] = {geo, dun};

    if (se_no < 0 || se_no >= 2800) {
        return nullptr;
    }
    if (basic_se_table_no >= 0 && se_no >= 100 && se_no < 300) {
        return basic[basic_se_table_no] + (se_no - 100);
    }
    if (se_no >= 300 && se_no < 400 && se_table_no >= 0 && cap_se_info[se_table_no] != nullptr) {
        return cap_se_info[se_table_no] + (se_no - 300);
    }
    if (se_no >= 400 && se_no < 500 && now_voice_set >= 0 && voice_info[now_voice_set] != nullptr) {
        return voice_info[now_voice_set] + (se_no - 400);
    }
    return &se_info[se_no];
}

int PortOf(const SND_SE_INFO *info) {
    return info->port >= 0 ? info->port : MIDI_PORT_SE_DEFAULT;
}

int HardwarePan(float pan) {
    return static_cast<int>(63.0f * std::clamp(pan, -1.0f, 1.0f)) + 64;
}

int Level(const SND_SE_INFO *info, int volume, float scale) {
    int level = volume;
    if (info->vol_no < 0) {
        level = 127;
    } else if (volume < 0) {
        level = CSnd.GetSeInfTbl()[info->vol_no * 2 + 1];
    }
    return std::clamp(static_cast<int>(static_cast<float>(level) * scale), 0, 127);
}

// coop-chestsound (Jep's pack 8 playtest, 2026-11-04: player 2 heard player 1 open a chest from across the floor): retail plays
// a presentation's sounds flat, as its opener is the one listening, but the player's presentation runs on the partner's copy
// too. His chest's and gate key's own sounds; their jingles are SndSPSePlay's (PlayerPresenting).
bool PresentationSound(int se_no) {
    return se_no == SE_CHEST_OPEN_BIG || se_no == SE_CHEST_OPEN_SMALL || se_no == SE_GATE_KEY_GET;
}

// Whether the game's task is one of the player's presentations (a chest's, an Atla's, a gate key's), which play the jingles.
bool PlayerPresenting() {
    if (PresentEngine()) { // coop-opt7: his presentation as this copy's engine (his copy alone)
        return true;
    }
    return mode == 3 && BtEventMode == 0 &&
           (gameTask == GAME_TASK_BOX_BIG_INIT || gameTask == GAME_TASK_BOX_BIG_LOOP || gameTask == GAME_TASK_BOX_SMALL_INIT ||
            gameTask == GAME_TASK_BOX_SMALL_LOOP || gameTask == GAME_TASK_ATRA_GET_INIT || gameTask == GAME_TASK_ATRA_GET_LOOP ||
            gameTask == GAME_TASK_GATE_KEY_LOOP);
}

// ---------------------------------------------------------------------------------------------
// The player's own unplaced sounds, placed where he stands on the partner's copy.

// What the player's fighting sounds: throws, drinks, hits on and by him, gems and bombs, his weapon
// breaking, Goro's smash, falling into water, and his voice set's sounds. His motions' own sound
// events are added from CharaMain's table.
bool PlayerSound(int se_no) {
    switch (se_no) {
        case SE_FIRE_GEM:
        case SE_ICE_GEM:
        case SE_THUNDER_GEM:
        case SE_WIND_GEM:
        case SE_HOLY_GEM:
        case SE_STATUS_AILMENT:
        case SE_EXPLOSION:
        case SE_THROW:
        case SE_DRINK:
        case SE_HIT_NO_DAMAGE:
        case SE_MONSTER_HIT:
        case SE_PLAYER_HIT:
        case SE_HIT_BLOCKED:
        case SE_WEAPON_BREAK:
        case SE_GORO_SMASH:
        case SE_WATER_SPLASH:
            return true;
        default:
            break;
    }
    if (se_no >= 400 && se_no < 500) {
        return true;
    }
    if (PresentationSound(se_no) && !AudioMutated("chestflat")) { // coop-chestsound: his presentations' (hers come placed)
        return true;
    }
    for (const CHARA_EVENT &event : CharaMain.event) {
        if (event.frame >= 0 && event.kind == static_cast<s32>(CharaEventKind::SoundEffect) && event.no == se_no) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------------------------
// Battle music: retail's crossfade (BtBattleMusic_Excg) run again on this copy's own distance.

struct Music {
    int   flag = 0;
    float vol = 0.0f;
    int   idle = kMusicIdleSteps;
    bool  held = false;
    int   log_wait = 0;
    int   logged_flag = -1;
} g_music;

void MusicExcg(float distance, float *field, float *battle) {
    if (g_music.flag != 0) {
        if (distance >= 110.0f) {
            g_music.vol -= 1.0f / 30.0f;
            if (g_music.vol < 0.1f) {
                g_music.flag = 0;
                g_music.vol = 0.0f;
            }
        } else if (g_music.vol < 0.9f) {
            g_music.vol += 1.0f / 30.0f;
        }
    }
    if (g_music.flag == 0 && distance <= 100.0f) {
        g_music.flag = 1;
    }
    g_music.vol = std::clamp(g_music.vol, 0.0f, 0.9f);
    *field = 1.0f - g_music.vol;
    *battle = g_music.vol;
}

void ReleaseMusic(bool reset) {
    if (g_music.held) {
        SoundCoopMusicScale(MIDI_PORT_BGM, 1.0f);
        SoundCoopMusicVolume(MIDI_PORT_AMBIENT, AmbientPortVolume(-1)); // (coop-onesound)
        SoundCoopStopBattle();
        g_music.held = false;
        if (Logging()) {
            std::printf("coop-audio: music released\n");
        }
    }
    if (reset) {
        g_music.flag = 0;
        g_music.vol = 0.0f;
    }
}

float NearestMonster(const float *from) {
    float         nearest = 10000.0f;
    sceVu0FVECTOR position;
    for (int i = 0; i < 16; i++) {
        if (NowMonstorUnit->monster[i].state == -1 || NowMonstorUnit->monster[i].revealed == 0) {
            continue;
        }
        NowMonstorUnit->chara[i][0].GetPosition(position);
        nearest = std::min(nearest, DistVector(const_cast<float *>(from), position));
    }
    return nearest;
}

// After retail's SetBattleStyle has stepped and set the game's volumes: this copy plays the mix of
// its own player's nearest monster instead. retail_* are what the game set.
void BattleMix(float player_distance, int preserve_bgm, float retail_field, float retail_battle) {
    float hear;
    if (AudioMutated("music")) {
        hear = player_distance;
    } else if (NetplayLocalPlayer() == 2 && PartnerOnFloor()) {
        hear = NearestMonster(g_partner.pos);
    } else if (!NetplayHasPlayer() && PartnerOnFloor()) {
        hear = std::min(player_distance, NearestMonster(g_partner.pos));
    } else {
        ReleaseMusic(true);
        return;
    }

    float field;
    float battle;
    MusicExcg(hear, &field, &battle);
    static bool fought = false;
    fought = fought || g_music.flag != 0;
    if (AudioMutated("bgm") && fought) {
        field = std::min(field, 0.1f);
    }
    g_music.idle = 0;
    g_music.held = true;

    // The field music by this copy's share over the game's (which is at least 0.1), so the game's own
    // fades still carry it.
    if (preserve_bgm == 0) {
        SoundCoopMusicScale(MIDI_PORT_BGM, field / std::max(retail_field, 0.1f));
    }

    const int battle_default = SoundCoopSequenceVolume(MIDI_PORT_AMBIENT, kBattleSequence);
    if (battle > 0.0f && battle_default >= 0 && SoundCoopStartBattle(kBattleSequence)) {
        SoundCoopMusicVolume(MIDI_PORT_AMBIENT, AmbientPortVolume(static_cast<int>(battle * battle_default))); // (coop-onesound)
    } else {
        SoundCoopStopBattle();
        SoundCoopMusicVolume(MIDI_PORT_AMBIENT, 0);
    }

    if (Logging() && (--g_music.log_wait <= 0 || g_music.flag != g_music.logged_flag)) {
        g_music.log_wait = 60;
        g_music.logged_flag = g_music.flag;
        std::printf("coop-audio: music player %d hear %.1f field %.2f battle %.2f bgm %d amb %d (game: player %.1f field %.2f battle %.2f)\n",
                    NetplayLocalPlayer(), hear, field, battle, SoundCoopPlayedVolume(MIDI_PORT_BGM),
                    SoundCoopPlayedVolume(MIDI_PORT_AMBIENT), player_distance, retail_field, retail_battle);
    }
}

// ---------------------------------------------------------------------------------------------
// The partner's voice: its motions' sound events and its hurts and falls, from its own voice set.

struct Voice {
    int   chara = -1;
    bool  loaded = false;
    int   set_no = -1;
    int   motion_no = -1;
    float time = 0.0f;
    int   invincible = 0;
    int   down = 0;
    int   swings = 0;
    bool  attack_hit = false;
    int   key_motion = -1; // coop-comboaudio: her attack's motion last step
} g_voice;

// coop-comboaudio (Jep's rc3 playtest 11-13: "P2 can do their full combo as Toan, but the audio always plays the sound from the first attack
// instead of the ones from the rest of the combo"): a melee member's key plays its swing and shout sounds at its motions' frames, each stage
// its own pair. These are the keys' tables, as they stand there: ToanKey_Play (charakey.cpp:165-176: 825/833/842/852/864 the five stages,
// 193 the leap's strike, 717 the rising slash), UngagaKey_Play (charakey.cpp:693-695) and GoroKey_Play (charakey.cpp:986-1004, half-open).
// The old cues (kStrikeCue: 400 and 420 on every hit) gave every stage the first one's. DC_COOP_AUDIO_MUTATE=combo_cue (the rows' twin): as before.
struct KeyCue {
    float from;
    float to;
    bool  closed;
    int   se_no;
};

constexpr KeyCue kToanCues[] = {
    {825.0f, 825.3f, true, 0x190}, {833.0f, 833.3f, true, 0x191}, {842.0f, 842.3f, true, 0x190}, {852.0f, 852.3f, true, 0x190},
    {864.0f, 864.3f, true, 0x191}, {193.0f, 193.3f, true, 0x191}, {717.0f, 717.3f, true, 0x191}, {825.0f, 825.3f, true, 0x1A4},
    {833.0f, 833.3f, true, 0x1A5}, {842.0f, 842.3f, true, 0x1A5}, {852.0f, 852.3f, true, 0x1A4}, {864.0f, 864.3f, true, 0x1A6},
};
constexpr KeyCue kUngagaCues[] = {{675.0f, 675.3f, true, 0x1A4}, {687.0f, 687.3f, true, 0x1A5}, {700.0f, 700.3f, true, 0x1A5}};
constexpr KeyCue kGoroCues[] = {
    {98.0f, 98.3f, false, SE_CHARA_SHOUT},     {471.0f, 471.4f, false, SE_CHARA_SHOUT_2}, {471.0f, 471.4f, false, SE_CHARA_ACTION_3},
    {478.0f, 478.4f, false, SE_CHARA_ACTION_3}, {481.0f, 481.4f, false, SE_CHARA_ACTION_4},
};

// The keys' sounds at a motion's start (their SndSePlay where the key moves to it): Toan's strike out of the air (3: SE_CHARA_SHOUT_3,
// charakey.cpp:542) and rising slash (0x18: SE_CHARA_SHOUT_4, charakey.cpp:462), Ungaga's charge pose (14 after his 13: SE_CHARA_SHOUT_3,
// charakey.cpp:849), Goro's smash (14: SE_GORO_SMASH, charakey.cpp:1091). -1 none.
int KeyStartCue(int chara, int motion, int was) {
    if (chara == CHARA_TOAN) {
        return motion == 3 ? SE_CHARA_SHOUT_3 : motion == 0x18 ? SE_CHARA_SHOUT_4 : -1;
    }
    if (chara == CHARA_UNGAGA) {
        return motion == 14 && was == 13 ? SE_CHARA_SHOUT_3 : -1;
    }
    if (chara == CHARA_GORO) {
        return motion == 14 ? SE_GORO_SMASH : -1;
    }
    return -1;
}

void PartnerSound(int se_no);

// Her key's sounds this step (her attack runs her member's key).
void StepKeyCues() {
    const int chara = g_voice.chara;
    const int motion = g_partner.attacking ? g_partner.attack_motion : -1;

    if (motion >= 0 && motion != g_voice.key_motion) {
        const int cue = KeyStartCue(chara, motion, g_voice.key_motion);
        if (cue >= 0) {
            PartnerSound(cue);
        }
    }
    g_voice.key_motion = motion;

    if (motion < 0) {
        return;
    }

    const KeyCue *cues = chara == CHARA_TOAN ? kToanCues : chara == CHARA_UNGAGA ? kUngagaCues : chara == CHARA_GORO ? kGoroCues : nullptr;
    const int     count = chara == CHARA_TOAN ? (int) std::size(kToanCues) : chara == CHARA_UNGAGA ? (int) std::size(kUngagaCues) : chara == CHARA_GORO ? (int) std::size(kGoroCues) : 0;
    const float   time = g_partner.chara->motion_type.state.time;

    for (int i = 0; i < count; i++) {
        if (time >= cues[i].from && (cues[i].closed ? time <= cues[i].to : time < cues[i].to)) {
            PartnerSound(cues[i].se_no);
        }
    }
}

// The voice cues of a swing, as retail's own attacks play them: at its start (BattleActionOn_Jinn's
// wind-up) and where it strikes (BattleActionPlay_Jinn's throw, BattleActionPlay_Ruby's shot, the
// sword combos' swing and shout in charakey.cpp), -1 for none. The partner's motions carry none.
constexpr int kStartCue[6] = {-1, SE_CHARA_ACTION, -1, -1, -1, -1};
constexpr int kStrikeCue[6][2] = {
    {SE_CHARA_ACTION, SE_CHARA_SHOUT},     {SE_CHARA_ACTION_2, SE_CHARA_SHOUT_2}, {SE_CHARA_ACTION, SE_CHARA_SHOUT},
    {SE_CHARA_ACTION, SE_CHARA_SHOUT},     {SE_CHARA_ACTION, SE_CHARA_SHOUT},     {SE_CHARA_ACTION, SE_CHARA_SHOUT},
};

// Plays one sound of the partner's at where it stands: its voice set's from the partner's port, the
// rest as any placed sound.
void PartnerSound(int se_no) {
    KeySoundRecord(2, se_no); // coop-comboaudio (the tests')
    float volume;
    float pan;
    SndGetVolPan(&volume, &pan, g_partner.pos, kNear, kFar);

    if (se_no >= 400 && se_no < 500) {
        const int          set_no = AudioMutated("voice") ? now_voice_set : g_voice.chara;
        const SND_SE_INFO *table = g_voice.loaded && set_no >= 0 ? voice_info[set_no] : nullptr;
        if (table == nullptr) {
            return;
        }
        const SND_SE_INFO *info = &table[se_no - 400];
        if (info->bank < 0) {
            return;
        }
        const int level = Level(info, -1, volume);
        SoundCoopVoice(info->bank, info->prog, HardwarePan(pan), level);
        if (Logging()) {
            std::printf("coop-audio: partner voice %d set %d program %d/%d vol %d pan %d (game's voice set %d)\n", se_no,
                        set_no, info->bank, info->prog, level, HardwarePan(pan), now_voice_set);
        }
        return;
    }
    SndSePlay(se_no, volume, pan, 0);
    if (Logging()) {
        std::printf("coop-audio: partner sound %d vol %.2f pan %.2f\n", se_no, volume, pan);
    }
}

void StepVoice() {
    if (!PartnerOnFloor()) {
        g_voice.motion_no = -1;
        return;
    }
    CCharacter *chara = g_partner.chara;

    if (g_partner.loaded_chara != g_voice.chara) {
        g_voice.chara = g_partner.loaded_chara;
        g_voice.loaded = g_voice.chara >= 0 && g_voice.chara < 11 && voice_info[g_voice.chara] != nullptr &&
                         SoundCoopLoadVoice(g_voice.chara);
        g_voice.motion_no = -1;
        g_voice.invincible = g_partner.invincible;
        g_voice.down = g_partner.down;
        g_voice.swings = g_partner.swings;
        g_voice.attack_hit = g_partner.attack_hit;
        if (Logging()) {
            for (const CHARA_EVENT &event : chara->event) {
                if (event.frame >= 0) {
                    std::printf("coop-audio: partner event motion set %d frame %d kind %d sound %d\n", event.motion_no,
                                event.frame, event.kind, event.no);
                }
            }
        }
    }

    // The motion's sound events, as CCharacter::Step fires them (the partner's are off there).
    int             index = -1;
    int             set_no = -1;
    tagMOTION_TYPE *motion = chara->motion_no >= 0 ? chara->GetMotionParam(chara->motion_no, &index, nullptr, nullptr, &set_no) : nullptr;
    if (motion != nullptr) {
        const float now = motion->state.time;
        const float step = now - g_voice.time;
        if (chara->motion_no == g_voice.motion_no && set_no == g_voice.set_no && step > 0.0f && step < 2.0f) {
            for (const CHARA_EVENT &event : chara->event) {
                if (event.frame < 0 || event.motion_no != set_no || static_cast<float>(event.frame) < g_voice.time ||
                    static_cast<float>(event.frame) >= now) {
                    continue;
                }
                if (event.kind == static_cast<s32>(CharaEventKind::SoundEffect)) {
                    PartnerSound(event.no);
                } else if (event.kind == static_cast<s32>(CharaEventKind::FootSound) && chara->foot_sound_enable != 0 && chara->foot_sound_id >= 0) {
                    SndPlayFootSound(chara->foot_sound_id, event.no, g_partner.pos);
                }
            }
        }
        g_voice.motion_no = chara->motion_no;
        g_voice.set_no = set_no;
        g_voice.time = now;
    }

    // A swing's start and its strike; a melee member's key, its own sounds at its frames (coop-comboaudio).
    const int  chara_no = std::clamp(g_voice.chara, 0, 5);
    const bool key = PartnerMeleeKey() && !AudioMutated("combo_cue");
    if (key) {
        StepKeyCues();
    } else {
        g_voice.key_motion = -1;
    }
    if (!key && g_partner.swings != g_voice.swings && g_partner.attacking && kStartCue[chara_no] >= 0) {
        PartnerSound(kStartCue[chara_no]);
    }
    if (!key && g_partner.attack_hit && !g_voice.attack_hit && g_partner.attacking) {
        for (int cue : kStrikeCue[chara_no]) {
            PartnerSound(cue);
        }
    }
    g_voice.swings = g_partner.swings;
    g_voice.attack_hit = g_partner.attack_hit;

    // A hit (TakeHits sets the partner's invincibility) and a fall (it lies down). Jep, 2026-11-01: a hit in her menu is a hit
    // (her hurt voice and her launch, as anywhere); 2026-10-20's "xiao whining sound" was the voice at every menu opening,
    // from a guard's invincibility, which the hit-rise rule below keeps quiet.
    // coop-soundpt: a guard (her key's presentation, a menu, a test) holds her unhurt with an invincibility of 2 each tick;
    // a hit gives her kInvincibleTicks. Only a hit's rise is a hurt (Jep's playtest, 2026-10-30: "p2 played hurt noise when
    // picking up the crest"). DC_COOP_AUDIO_MUTATE=guardhurt: any rise, as before (twin).
    const bool hit_rise = g_partner.invincible > 2 || AudioMutated("guardhurt");
    if (g_partner.down > 0 && g_voice.down == 0) {
        PartnerSound(SE_CHARA_KNOCKED_OUT);
    } else if (g_partner.invincible > g_voice.invincible && hit_rise && g_partner.down == 0 &&
               !(AudioMutated("menu_hurt_muted") && FullMenuHoldsPartner())) { // (twin: muted in her menu, as before)
        PartnerSound(SE_CHARA_HURT);
    }
    g_voice.invincible = g_partner.invincible;
    g_voice.down = g_partner.down;
}

} // namespace

// coop-wellsound (Jep's rc4 playtest: "P2 on Xiao hears Toan's sound effect when entering the well", the floor's healing fountain):
// her own recover voice. Retail's HealingWater and usedActiveItem play SE_CHARA_RECOVER (440) with SndSePlay, which takes a
// 400-499 sound from the game's voice set (now_voice_set): the controlled member's, so Xiao's when player 1 plays Xiao. Her
// spring heal and her items called the same SndSePlay, and the game's set is player 1's member's on every copy: Toan's "ahh"
// for Xiao. Now from her member's set at where she stands, on every copy (PartnerSound), as her hurt and fall voices are.
// DC_COOP_AUDIO_MUTATE=gamevoice (twin): the game's voice set, as before.
void PartnerVoiceSound(int se_no) {
    const bool game = AudioMutated("gamevoice");
    // the voice set the sound is taken from (PartnerSound's: her member's, loaded on her body's load)
    const int set_no = game ? now_voice_set : g_voice.loaded ? g_voice.chara : -1;

    std::fprintf(stderr, "coop-audio: player 2's voice sound %d from voice set %d (her member %d, the game's set %d) on player %d's copy at link tick %lld\n",
                 se_no, set_no, g_partner.loaded_chara, now_voice_set, NetplayLocalPlayer(), (long long) NetplayLinkTick());
    if (game) {
        SndSePlay(se_no, -1, 0);
        return;
    }
    PartnerSound(se_no);
}

// coop-gapvoice (the developer's playtest 3: "xiao jumping over the xiao-only jumping pits plays toan's attacking noise"): a sound of player 2's
// own (a SoundOwner's: her gap jump's program, which runs beside the game in her name, its _PLAY_SE) that is a voice (400-499) came
// through retail's SndSePlay, which takes every voice from the game's voice set: player 1's member's on every copy, Toan's for Xiao.
// Retail never met it (only the controlled member jumps a gap, so the game's set was the jumper's). Now from her member's own set,
// where she stands, as her other voices (PartnerSound), on her copy alone (another player's own sound: OthersSound).
// DC_COOP_AUDIO_MUTATE=ownvoice_gameset (twin): the game's voice set, as before.
bool HerOwnVoiceSound(int se_no) {
    if (se_no < 400 || se_no >= 500 || g_owner.player != 2 || !Enabled() || AudioMutated("gamevoice")) {
        return false;
    }
    if (OthersSound(se_no)) {
        return true; // (hers: not played on another player's copy)
    }
    const bool game = AudioMutated("ownvoice_gameset");

    std::fprintf(stderr, "coop-audio: player 2's own voice sound %d from voice set %d (her member %d, the game's set %d) on player %d's copy at link tick %lld\n",
                 se_no, game ? now_voice_set : g_voice.loaded ? g_voice.chara : -1, g_partner.loaded_chara, now_voice_set, NetplayLocalPlayer(),
                 (long long) NetplayLinkTick());
    if (game) {
        return false;
    }
    PartnerSound(se_no);
    return true;
}

void AudioSnapshotBefore() {
    g_bgm_driver_no = now_bgm_no;
    g_bgm_driver_play = static_cast<int>(now_bgm_play);
}

void AudioSnapshotApplied() {
    const int   set_no = now_bgm_no;
    const int   set_play = static_cast<int>(now_bgm_play);
    const int   set_vol = now_bgm_vol;
    const float set_fade = now_bgm_fade_vol;

    if (AudioMutated("no_bgm_carry") || (set_no == g_bgm_driver_no && set_play == g_bgm_driver_play)) {
        return;
    }

    now_bgm_no = g_bgm_driver_no; // (the words as the driver has it: the game's own calls below then do their work)
    now_bgm_play = static_cast<BgmPlayState>(g_bgm_driver_play);

    if (set_no < 0) {
        SndBgmStop();
        now_bgm_no = set_no;
    } else {
        if (set_no != g_bgm_driver_no) {
            SndBgmLoad(set_no);
        }
        if (set_play == 1) {
            SndBgmPlay(0);
        } else {
            SndBgmStop();
        }
    }
    now_bgm_play = static_cast<BgmPlayState>(set_play);
    now_bgm_vol = set_vol;
    now_bgm_fade_vol = set_fade;
    std::fprintf(stderr, "coop-audio: music after the state set: track %d playing %d (this copy's driver had track %d playing %d)\n", set_no, set_play, g_bgm_driver_no, g_bgm_driver_play);
    std::fflush(stderr);
}

// coop-atlaopen
namespace {

bool HearsPartnerPresentation() {
    const int local = NetplayLocalPlayer();
    return local == 0 || local == 2;
}

} // namespace

void PartnerSpecialLoad(int set_no) {
    if (AudioMutated("specialse") || !HearsPartnerPresentation()) {
        return;
    }
    SoundCoopLoadSpecial(set_no);
}

void PartnerSpecialSound(int set_no, int se_no) {
    const int local = NetplayLocalPlayer();

    if (AudioMutated("specialse") || !HearsPartnerPresentation()) {
        if (Logging()) {
            std::printf("coop-audio: partner special sound %d of set %d left out on player %d's copy\n", se_no, set_no, local);
        }
        return;
    }

    // As SndSPSePlay: the row's bank and program, at its effect-table level (vol_no), else 64.
    const SND_SE_INFO *info = se_no >= 0 && se_no < 64 ? &special_se_info[se_no] : nullptr;
    const int          level = info != nullptr && info->vol_no >= 0 ? SoundCoopEffectVolume(info->vol_no) : -1;
    const bool         played = info != nullptr && info->bank >= 0 && SoundCoopLoadSpecial(set_no) &&
                        SoundCoopSpecial(info->bank, info->prog, level >= 0 ? level : 64);

    if (Logging()) {
        std::printf("coop-audio: partner special sound %d of set %d %s on player %d's copy\n", se_no, set_no,
                    played ? "played" : "missing", local);
    }
}

namespace {
const float *g_from = nullptr; // coop-soundpt: where the sounds played now come from (SoundFrom), nullptr: flat as retail's
} // namespace

SoundFrom::SoundFrom(const float *at) : at_(g_from) {
    g_from = at;
}

SoundFrom::~SoundFrom() {
    g_from = at_;
}

namespace {
bool g_monster_step = false; // coop-farcull: inside motionDrive's monster step (MonsterSounds)
} // namespace

MonsterSounds::MonsterSounds() : was_(g_monster_step) {
    g_monster_step = true;
}

MonsterSounds::~MonsterSounds() {
    g_monster_step = was_;
}

// coop-farcull: the monster stepping now, when its flat sound is to be placed (MonsterSounds); its place and fall-off.
// *motion: the sound is its motion's (SoundCheck's own, which retail turns down after it starts with SndSetSeVolf); any other (its
// script's SET_SND_NOW, the unit's hit sounds) was flat.
static bool MonsterSoundPlace(int se_no, float *at, float *near_dist, float *far_dist, float *from_own, float *wake, bool *motion) {
    if (!g_monster_step || !Enabled() || !NetplayHasPlayer() || NowMonstorUnit == nullptr) {
        return false;
    }
    const int i = NowMonstorUnit->current_monster;

    if (i < 0 || i >= 16) {
        return false;
    }
    const MONSTOR_SOUND &sounds = NowMonstorUnit->sound[i];

    *motion = sounds.sequence_id == se_no;
    for (int k = 0; k < 16; k++) {
        *motion = *motion || sounds.id[k] == se_no;
    }
    if (AudioMutated("monsterflat")) { // (the twin's: said, so its row ends on it)
        if (Logging()) {
            std::printf("coop-audio: monster %d's sound %d flat on player %d's copy (monsterflat), %s\n", i, se_no, NetplayLocalPlayer(),
                        *motion ? "its motion's" : "a flat one");
        }
        return false;
    }
    sceVu0FVECTOR own;

    if (ActorLocalIndex() == 1) {
        sceVu0CopyVector(own, g_partner.pos);
    } else {
        PlayerDrawnAt(own);
    }
    NowMonstorUnit->chara[i][0].GetPosition(at);
    at[3] = 1.0f;
    own[3] = 1.0f;
    *from_own = DistVector(own, at);
    *wake = NowMonstorUnit->monster[i].clip_distance;
    *near_dist = NowMonstorUnit->monster[i].kind == MONSTER_KIND_NO_LOCK_ON ? 350.0f : 50.0f;
    *far_dist = NowMonstorUnit->monster[i].kind == MONSTER_KIND_NO_LOCK_ON ? 1000.0f : 500.0f;
    const bool placed = *from_own >= *wake; // (inside it retail steps the monster for this player: its sounds as retail's)

    if (!placed && Logging()) {
        std::printf("coop-audio: monster %d's sound flat as retail's, %.0f from this copy's player (wakes within %.0f) on player %d's copy\n", i, *from_own,
                    *wake, NetplayLocalPlayer());
    }
    return placed;
}

void PartnerWeaponWarning() {
    static int count = 0;
    const int  chara = g_partner.loaded_chara;

    if (!Enabled() || g_partner.chara == nullptr || mode != 3 || chara < 0 || chara >= 6 || UserStatus == nullptr || BtEventMode != 0) {
        count = 0;
        return;
    }

    const WEAPON_HAVE &weapon = UserStatus->chara_weapons[chara][(int) UserStatus->equipped_weapon_slot[chara]];

    if (weapon.item_no <= 0 || weapon.durability_f > 10.0f || weapon.item_no == defWeapon__6[chara] || AudioMutated("herwarning")) {
        count = 0;
        return;
    }

    // As motionDrive's: again every durability x 10 ticks, at least 10.
    if (count > 0) {
        count--;
        return;
    }

    count = std::max(10, (int) weapon.durability_f * 10);

    // coop-router: her beep is hers (present::For forwards SoundOwner). Test hook (DC_COOP_TEST_PRESENT_ROLL=1, x-dungeon-p2
    // -Case guard): the beep rolls the generator once, as a presentation that wrote the step's state would, for the guard.
    const present::Scope hers(present::For{2}, COOP_PRESENT_HERE);

    if (static const bool roll = TestEnvOn("DC_COOP_TEST_PRESENT_ROLL"); roll) {
        rand();
    }

    SndSePlay(SE_WARNING, -1, 0);
}

SoundOwner::SoundOwner(int player, bool menu_only) : player_(g_owner.player), menu_only_(g_owner.menu_only) {
    g_owner.player = player;
    g_owner.menu_only = menu_only;
}

SoundOwner::~SoundOwner() {
    g_owner.player = player_;
    g_owner.menu_only = menu_only_;
}

int SoundOwnerPlayer() {
    return g_owner.player;
}

// coop-warmreset (WS7): the partner voice's edge memory as a fresh copy has it, so the job's first step copies the partner's state
// without sounding (a stale invincible, down or swing count fired or skipped a cue right after the load).
void AudioResetForJob() {
    g_voice = Voice{};
}

int VoiceJobChara() {
    return g_voice.chara;
}

// ---------------------------------------------------------------------------------------------
// coop-onesound (Jep's v0.2.3 playtest, 2026-11-22, two windows on one PC: "I hear audio from torches maybe on both characters
// fairly loudly"): every window on a PC plays into the same speakers, so the world's shared sounds played once per window. Divine
// Beast Cave's background loop (SE 53, which retail sets from the torch nearest the camera: one is always near, 46-58 of 127 on
// both copies) came twice, as did the music and every other background loop. Now a window player 1's copy started on this PC
// (Local's player 2, a lobby's Multi screen seat or handed window: NetplayLocalSpawn gives each DC_NETPLAY_LOCAL_CLAIM or
// DC_NETPLAY_LOCAL_PAD), while linked to it and in its world (not apart), leaves the shared sounds to the lowest player on the PC
// (player 1's copy, the one that started it): the music ports (the music, the ambient/battle port, the third sequence port), the
// background loops (the base table's ambient block, se 50-66: the floors' loops, the town's water, fire and wind sources) and
// thunder (67-74). Its loops still run, at volume 0, so the moment player 1's copy goes (the link down: it closed or dropped) or
// the pair goes apart, this window plays them again from the next frame. Each player's own sounds (steps, swings, hits, voice,
// menus) and the monsters' stay on every window, as before; copies on other PCs are as before.
// DC_COOP_AUDIO_TEST_SAME_PC=1 (the tests'): this copy counts as such a window. DC_COOP_AUDIO_MUTATE=onesound_all (twin): every
// window plays everything, as before. DC_COOP_AUDIO_LOG: a "coop-onesound:" line a second with what this copy played and held.
bool SamePcWindow() {
    static const bool started = TestEnvInt("DC_NETPLAY_LOCAL_CLAIM", -1) >= 0 || TestEnvInt("DC_NETPLAY_LOCAL_PAD", -1) >= 0 ||
                                TestEnvOn("DC_COOP_AUDIO_TEST_SAME_PC");
    return started;
}

bool SharedSoundsElsewhere() {
    return SamePcWindow() && NetplayLocalPlayer() > 1 && NetplayLinkUp() && !CoopWorldsApart() && !AudioMutated("onesound_all");
}

bool SharedLoopSe(int se_no) {
    return se_no >= 50 && se_no <= 66;
}

bool SharedFlatSe(int se_no) {
    return se_no >= SE_THUNDER_RUMBLE && se_no <= SE_THUNDER_CLAP;
}

// The ambient port's volume co-op asks for (BattleMix, ReleaseMusic): 0 while the shared sounds are another window's.
int AmbientPortVolume(int volume) {
    return SharedSoundsElsewhere() ? 0 : volume;
}

// What this copy did with its shared and own sounds since its last line (DC_COOP_AUDIO_LOG).
struct OneSound {
    int loops = 0;     // background loop volumes the game set
    int loops_top = 0; // the loudest of them
    int loop_se = -1;  // and its sound
    int held = 0;      // of them, played at 0 here
    int flat_held = 0; // thunder not played here
    int own = 0;       // other sounds played
} g_one;

} // namespace coop

// snd.cpp's SndSetSeVol (coop-onesound): the volume a background loop plays at on this copy.
int CoopSharedSeVolume(int se_no, int vol) {
    if (!coop::SharedLoopSe(se_no)) {
        return vol;
    }
    coop::g_one.loops++;
    if (vol >= coop::g_one.loops_top) {
        coop::g_one.loops_top = vol;
        coop::g_one.loop_se = se_no;
    }
    if (coop::SharedSoundsElsewhere()) {
        coop::g_one.held++;
        return 0;
    }
    return vol;
}

void CoopAudioStep() {
    using namespace coop;

    if (g_music.held && ++g_music.idle >= kMusicIdleSteps) {
        ReleaseMusic(true);
    }
    StepVoice();

    // coop-onesound: the music ports, held at 0 on a window whose shared sounds are player 1's copy's, the game's again after.
    static bool shared_held = false;
    const bool  hold = SharedSoundsElsewhere();
    if (hold) {
        SoundCoopMusicVolume(MIDI_PORT_BGM, 0);
        SoundCoopMusicVolume(MIDI_PORT_AMBIENT, 0);
        SoundCoopMusicVolume(MIDI_PORT_UNK_2, 0);
    } else if (shared_held) {
        SoundCoopMusicVolume(MIDI_PORT_BGM, -1);
        SoundCoopMusicVolume(MIDI_PORT_AMBIENT, -1); // (BattleMix asks for its own again at its next step)
        SoundCoopMusicVolume(MIDI_PORT_UNK_2, -1);
    }
    if (hold != shared_held) {
        shared_held = hold;
        std::printf("coop-onesound: this copy (player %d) %s at link tick %lld\n", NetplayLocalPlayer(),
                    hold ? "is a window on player 1's PC, linked in one world: its music and background loops play from player 1's copy"
                         : "plays its own music and background loops again (player 1's copy gone, the pair apart, or not on one PC)",
                    (long long) NetplayLinkTick());
        std::fflush(stdout);
    }
    if (static int one_wait = 0; Logging() && --one_wait <= 0) {
        one_wait = 60;
        std::printf("coop-onesound: player %d tick %lld: shared %s; loops %d (loudest se %d at %d), held at 0 %d; thunder held %d; "
                    "music ports played %d %d %d; other sounds played %d\n",
                    NetplayLocalPlayer(), (long long) NetplayLinkTick(), hold ? "elsewhere" : "here", g_one.loops, g_one.loop_se, g_one.loops_top,
                    g_one.held, g_one.flat_held, SoundCoopPlayedVolume(MIDI_PORT_BGM), SoundCoopPlayedVolume(MIDI_PORT_AMBIENT),
                    SoundCoopPlayedVolume(MIDI_PORT_UNK_2), g_one.own);
        g_one = OneSound{};
    }

    static bool said = false;
    if (Logging() && !said) {
        said = true;
        std::printf("coop-audio: on, mutation [%s]\n", TestEnvOr("DC_COOP_AUDIO_MUTATE", "none"));
    }

    static int log_wait = 0;
    if (Logging() && PartnerOnFloor() && --log_wait <= 0) {
        log_wait = 60;
        // The floor music this copy plays, by the monster nearest its own player (both in couch co-op).
        float near = 10000.0f;
        if (NowMonstorUnit != nullptr) {
            const int player = NetplayLocalPlayer();
            near = player == 2 ? NearestMonster(g_partner.pos) : NearestMonster(CharaMain.pos);
            if (player == 0) {
                near = std::min(near, NearestMonster(g_partner.pos));
            }
        }
        std::printf("coop-audio: bgm player %d near %.1f playing %d played %d\n", NetplayLocalPlayer(), near,
                    SoundCoopPlaying(MIDI_PORT_BGM) ? 1 : 0, SoundCoopPlayedVolume(MIDI_PORT_BGM));
        std::printf("coop-audio: listener player %d partner_listens %d at %.1f %.1f, player %.1f %.1f, partner %.1f %.1f\n",
                    NetplayLocalPlayer(), PartnerListens() ? 1 : 0, camera_pos[0], camera_pos[2], CharaMain.pos[0],
                    CharaMain.pos[2], g_partner.pos[0], g_partner.pos[2]);
    }
}

// retail's, and on the partner's copy the player's fighting sounds are placed where he stands.
namespace dc::game { // repin: was PORT_OVERRIDE; a recipe removes the game's own definition
void SndSePlay(int se_no, int vol, int voice) {
    if (coop::HerOwnVoiceSound(se_no)) { // coop-gapvoice: her own voice from her member's set
        return;
    }
    coop::KeySoundRecord(1, se_no); // coop-comboaudio (the tests'): the player's own key sounds
    // coop-soundpt: the game's one SE_WARNING is motionDrive's beep for player 1's weapon about to break: his alone (Jep's
    // playtest, 2026-10-30: player 2 heard it). Player 2's own (PartnerWeaponWarning) comes owned by her.
    // DC_COOP_AUDIO_MUTATE=hiswarning: nobody's, on both copies, as before (twin).
    const bool       drive_warning = se_no == SE_WARNING && !coop::OwnedNow(se_no) && !coop::AudioMutated("hiswarning"); // (a menu-only owner around it: still nobody's)
    coop::SoundOwner warning_owner(drive_warning ? 1 : coop::g_owner.player, drive_warning ? false : coop::g_owner.menu_only);
    SND_SE_INFO     *info = coop::SeInfo(se_no);

    if (info == nullptr || coop::OthersSound(se_no)) { // coop-menuse
        return;
    }
    // coop-onesound: on a window whose shared sounds are player 1's copy's, a background loop starts at 0 (it runs, so it is heard
    // here the moment that copy goes; its volume is SndSetSeVol's) and thunder is not played here.
    if (coop::SharedSoundsElsewhere() && (coop::SharedLoopSe(se_no) || coop::SharedFlatSe(se_no))) {
        if (coop::SharedFlatSe(se_no)) {
            coop::g_one.flat_held++;
            return;
        }
        CSnd.SE_Play(coop::PortOf(info), info->bank, info->prog, 0, voice); // (at 0 even for a row with no volume of its own)
        return;
    } else if (!coop::SharedLoopSe(se_no)) {
        coop::g_one.own++;
    }

    // coop-soundpt: a sound with a place of its own (SoundFrom: her step's, a hit where it lands) comes from there, unless it is
    // one player's alone (a menu's); before the player's own placing below, which would put her hits where Toan stands.
    if (coop::g_from != nullptr && !coop::OwnedNow(se_no) && coop::Enabled() && !coop::AudioMutated("partnerflat")) {
        float volume;
        float pan;
        SndGetVolPan(&volume, &pan, const_cast<float *>(coop::g_from), coop::kNear, coop::kFar);
        const int level = coop::Level(info, vol, volume);
        CSnd.SE_Play(coop::PortOf(info), info->bank, info->prog, coop::HardwarePan(pan), 127, level, voice);
        if (coop::Logging()) {
            std::printf("coop-audio: partner-placed sound %d vol %d pan %d, %.0f from the listener on player %d's copy\n", se_no, level,
                        coop::HardwarePan(pan), DistVector(camera_pos, const_cast<float *>(coop::g_from)), NetplayLocalPlayer());
        }
        return;
    }

    // coop-farcull: a monster's flat sound, from the monster when only another player woke it (MonsterSounds).
    if (!coop::OwnedNow(se_no)) {
        sceVu0FVECTOR at;
        float         near_dist;
        float         far_dist;
        float         from_own;
        float         wake;
        bool          motion = false;

        if (coop::MonsterSoundPlace(se_no, at, &near_dist, &far_dist, &from_own, &wake, &motion)) {
            float volume;
            float pan;
            SndGetVolPan(&volume, &pan, at, near_dist, far_dist);
            const int level = coop::Level(info, vol, volume);
            CSnd.SE_Play(coop::PortOf(info), info->bank, info->prog, coop::HardwarePan(pan), 127, level, voice);
            if (coop::Logging()) {
                std::printf("coop-audio: monster %d's sound %d placed vol %d pan %d, %.0f from the listener, %.0f from this copy's player (wakes within %.0f) on player %d's copy, %s\n",
                            NowMonstorUnit->current_monster, se_no, level, coop::HardwarePan(pan), DistVector(camera_pos, at), from_own, wake, NetplayLocalPlayer(),
                            motion ? "its motion's" : "a flat one");
            }
            return;
        }
    }

    // coop-entrance2 (the cave's carried walk-in, DCO-45): his sounds placed where he is drawn, not at CharaMain.pos ((0 0 0) while a
    // scene's node carries him: his steps down the cave's stairs went silent on her copy). DC_COOP_MUTATE=farplayer_main_pos: as before.
    sceVu0FVECTOR player_drawn;

    coop::PlayerDrawnAt(player_drawn);
    if (coop::PartnerListens() && coop::PlayerSound(se_no) && !coop::AudioMutated("flat")) {
        float volume;
        float pan;
        SndGetVolPan(&volume, &pan, player_drawn, coop::kNear, coop::kFar);
        const int level = coop::Level(info, vol, volume);
        CSnd.SE_Play(coop::PortOf(info), info->bank, info->prog, coop::HardwarePan(pan), 127, level, voice);
        if (coop::Logging()) {
            std::printf("coop-audio: player sound %d placed vol %d pan %d, listener %.0f from the player\n", se_no, level,
                        coop::HardwarePan(pan), DistVector(camera_pos, player_drawn));
        }
        return;
    }

    if (coop::Logging() && coop::PresentationSound(se_no)) { // coop-chestsound
        std::printf("coop-audio: sound %d flat on player %d's copy\n", se_no, NetplayLocalPlayer());
    }
    if (info->vol_no < 0) {
        vol = 127;
    }
    if (vol < 0) {
        CSnd.SE_Play(coop::PortOf(info), info->vol_no, voice);
    } else {
        CSnd.SE_Play(coop::PortOf(info), info->bank, info->prog, vol, voice);
    }
}
} // namespace dc::game

// retail's, but (coop-chestsound) on the partner's copy the player's presentations' jingles (a chest's acquired jingle, an
// Atla's chime, a gate key's) come from where he stands, as his chest's own sound does.
namespace dc::game { // repin: was PORT_OVERRIDE; a recipe removes the game's own definition
void SndSPSePlay(int se_no, int vol) {
    sceVu0FVECTOR player_drawn; // (coop-entrance2, DCO-45: where he is drawn)

    coop::PlayerDrawnAt(player_drawn);
    SND_SE_INFO *info = se_no >= 0 && se_no < 64 ? &special_se_info[se_no] : nullptr;

    if (info == nullptr) {
        return;
    }
    if (info->vol_no < 0) {
        vol = 64;
    }

    if (coop::PartnerListens() && coop::PlayerPresenting() && !coop::AudioMutated("chestflat")) {
        float volume;
        float pan;
        SndGetVolPan(&volume, &pan, player_drawn, coop::kNear, coop::kFar);
        const int base = vol >= 0 ? vol : CSnd.GetSeInfTbl()[info->vol_no * 2 + 1];
        const int level = std::clamp(static_cast<int>(static_cast<float>(base) * volume), 0, 127);
        CSnd.SE_Play(MIDI_PORT_SE_SPECIAL, info->bank, info->prog, coop::HardwarePan(pan), 127, level, 0);
        if (coop::Logging()) {
            std::printf("coop-audio: player special sound %d placed vol %d pan %d, listener %.0f from the player\n", se_no, level,
                        coop::HardwarePan(pan), DistVector(camera_pos, player_drawn));
        }
        return;
    }

    if (coop::Logging() && coop::PlayerPresenting()) {
        std::printf("coop-audio: special sound %d flat on player %d's copy\n", se_no, NetplayLocalPlayer());
    }
    if (vol < 0) {
        CSnd.SE_Play(MIDI_PORT_SE_SPECIAL, info->vol_no, 0);
    } else {
        CSnd.SE_Play(MIDI_PORT_SE_SPECIAL, info->bank, info->prog, vol, 0);
    }
}
} // namespace dc::game

// retail's, then this copy's own battle music (BattleMix).
namespace dc::game { // repin: was PORT_OVERRIDE; a recipe removes the game's own definition
float SetBattleStyle(int map_no, int preserve_bgm) {
    float         nearest = 10000;
    sceVu0FVECTOR position;
    sceVu0FVECTOR player;
    float         bgm_volume = 0.0f;     // 9418a550 starts both at 0
    float         ambient_volume = 0.0f;

    sceVu0CopyVector(player, CharaMain.pos);

    for (int i = 0; i < 16; i++) {
        if (NowMonstorUnit->monster[i].state == -1) {
            continue;
        }
        if (NowMonstorUnit->monster[i].revealed == 0) {
            continue;
        }
        NowMonstorUnit->chara[i][0].GetPosition(position);
        float distance = DistVector(player, position);
        if (distance < nearest) {
            nearest = distance;
        }
    }

    if (map_no != 5) {
        int floors = coop::kMaxFloor[map_no];

        if (UserStatus->cur_floor < floors - 1) {
            BtBattleMusic_Excg(nearest, &bgm_volume, &ambient_volume);

            if (ambient_volume > 0.0f) {
                SndAmbientPlay(0);
            }

            if (preserve_bgm == 0) {
                SndSetBgmVolf(bgm_volume);
            }

            SndAmbientSetVolf(ambient_volume);
            coop::BattleMix(nearest, preserve_bgm, bgm_volume, ambient_volume); // coop: audio
        }
    }

    return nearest;
}
} // namespace dc::game

// retail's, and this copy's own battle music goes with the game's.
namespace dc::game { // repin: was PORT_OVERRIDE; a recipe removes the game's own definition
void BtBattleMusic_Init() {
    battle_music_active = false;
    battle_music_wait = 0;
    battle_music_volume = 0.0f;
    coop::ReleaseMusic(true); // coop: audio
}
} // namespace dc::game

namespace dc::game { // repin: was PORT_OVERRIDE; a recipe removes the game's own definition
void BtBattleMusic_Stop() {
    coop::ReleaseMusic(true); // coop: audio
    if (battle_music_active) {
        SndSetBgmVolf(1.0f);
        SndAmbientSetVolf(0.0f);
        BtBattleMusic_Init();
    }
}
} // namespace dc::game

// retail's, but on player 2's copy walking a town with her own camera the town's placed sounds (EdSetSoundSrcVol: rivers,
// fires, doors; then the footsteps) listen from her camera, not the game's (coop-soundpt, CoopTownListener). The town's
// sources are still gathered round the game's camera (its parts in reach), only heard from hers.
namespace dc::game { // repin: was PORT_OVERRIDE; a recipe removes the game's own definition
void SndSetCamera(float *position, float *rotation) {
    sceVu0CopyVector(camera_pos, position);
    sceVu0CopyVector(camera_dir, rotation);

    if (mode == 2 && !coop::AudioMutated("listener")) { // GAME_MODE_EDIT
        CoopTownListener(camera_pos, camera_dir);
    }

    // DC_COOP_AUDIO_LOG: where player 2's copy listens in town against where she and Toan stand, once a second.
    static int said = 0;
    float      her[4] = {0.0f, 0.0f, 0.0f, 1.0f};

    if (coop::Logging() && mode == 2 && NetplayLocalPlayer() == 2 && Chara != nullptr && coop::TownPartnerShown(her, nullptr) && ++said % 60 == 1) {
        sceVu0FVECTOR toan;

        Chara->GetPosition(toan);
        std::printf("coop-audio: town listener at (%.0f %.0f) on player 2's copy, %.0f from her, %.0f from Toan\n", camera_pos[0], camera_pos[2],
                    DistVector(camera_pos, her), DistVector(camera_pos, toan));
    }
}
} // namespace dc::game

// coop-pt1108: the listener set again as it stands, through the town's own path above, for a tick no town walk sets it (CoopTownListenerTick).
void CoopAudioListenerAgain() {
    SndSetCamera(camera_pos, camera_dir);
}

// retail's: the sound listens from the camera the copy draws from (the partner's on its own copy).
namespace dc::game { // repin: was PORT_OVERRIDE; a recipe removes the game's own definition
void SndSetCamera(CCamera *camera) {
    if (coop::AudioMutated("listener")) {
        camera = (CCamera *) &MainCamera__4;
    }
    camera->GetPos(camera_pos);
    camera->GetDir(camera_dir);
}
} // namespace dc::game
