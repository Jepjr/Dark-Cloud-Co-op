#pragma once

#include "coop/game_ns.hpp"

// coop: audio. What each copy hears, from its own player. Sound is presentation only: nothing here
// draws the random generator or writes anything the game reads, so the copies' simulation and the
// link's checksum are the same whatever each copy hears.
//
// Listener: each copy pans and fades its placed sounds (SndGetVolPan) against the camera it draws
// from (SndSetCamera in DrawDungeon), so player 2's copy hears from Xiao's camera while the cameras
// are independent. Couch co-op has one camera, which frames both players, so it listens from there.
// On player 2's copy the player's own unplaced sounds (his swings, shouts, hurts and throws, which
// retail plays flat because the camera is always on him) are placed where he stands.
//
// Battle music: retail crossfades the field and battle music by the monster nearest the player
// (SetBattleStyle). Each copy keeps retail's game state as it is and plays the crossfade by its own
// player's nearest monster instead: player 2's copy by the partner's, couch co-op by whichever of
// the two is nearer one.
//
// Voice: the partner's voice set plays on a mixer port of its own (kCoopVoicePort), so the partner's
// shouts, hurts and falls are its own character's while the game's voice set stays the player's.
//
// Menu sounds (coop-menuse): the menus step on both linked copies for the lockstep, so their sounds played on both
// (Jep's 2026-10-25 "Both players can hear each others menu noises"; "Xiao pressing square or circle in the dungeon
// plays the menu cancel noise and p1 can hear it"). A menu's step and draw, and player 2's own input on a floor, say
// whose they are (coop::SoundOwner); a sound played inside one plays on its owner's copy only (couch: the one copy).

// The mixer port the partner's voice set is bound to: one the game never plays on.
constexpr int kCoopVoicePort = 9;
// coop-atlaopen: the mixer port player 2's special set (her presentations' sounds) is bound to: one the game never plays on.
constexpr int kCoopSpecialPort = 8;

// CSound::Step calls it once a step, before the step's effects are sent.
void CoopAudioStep();

// sound.cpp: the mixer for this copy alone.
// Plays port at volume instead of the game's (-1: the game's again).
namespace dc::game {
void SoundCoopMusicVolume(int port, int volume);
// Plays port at the game's volume times scale (1: as the game sets it), so the game's fades go on.
void SoundCoopMusicScale(int port, float scale);
// The volume the mixer plays port at on this copy.
int SoundCoopPlayedVolume(int port);
// Whether a sequence plays on port.
bool SoundCoopPlaying(int port);
// The sequence table's volume of a port's sequence, -1 for none.
int SoundCoopSequenceVolume(int port, int seq_no);
// Starts the ambient port's sequence seq_no when nothing plays there; true when something does.
bool SoundCoopStartBattle(int seq_no);
// Stops what SoundCoopStartBattle started, unless the game has since started it too.
void SoundCoopStopBattle();
// Binds a character's voice set (sound/voice/voiceN.snd) to kCoopVoicePort.
bool SoundCoopLoadVoice(int set_no);
// Plays one program of the partner's voice set.
void SoundCoopVoice(int bank, int program, int pan, int volume);
// coop-atlaopen: binds special set set_no (sound/special/spN.snd) to kCoopSpecialPort (already bound: nothing to do).
bool SoundCoopLoadSpecial(int set_no);
// Plays one program of it, centred; false when no set is bound.
bool SoundCoopSpecial(int bank, int program, int volume);
// The game's effect table's volume of row se_no, -1 for none.
int SoundCoopEffectVolume(int se_no);
} // namespace dc::game

namespace coop {
void AudioSnapshotBefore();  // coop-pt1109c: the sound driver's track, before a state set lands over the words for it
void AudioSnapshotApplied(); // coop-pt1109c: after: the driver made to match the set

// coop-menuse: the sounds played while one lives are player's (1 or 2; 0 everyone's, as retail's), heard on that
// player's copy alone. menu_only: only the menu's own sounds (cursor, confirm, refuse; lock-on) are the player's,
// for input that also plays the world's (player 2's swings, items and lock on a floor). They nest.
// DC_COOP_AUDIO_MUTATE=menuse (the twin): every copy plays them, as before.
class SoundOwner {
public:
    explicit SoundOwner(int player, bool menu_only = false);
    ~SoundOwner();
    SoundOwner(const SoundOwner &) = delete;
    SoundOwner &operator=(const SoundOwner &) = delete;

private:
    int  player_;
    bool menu_only_;
};

// coop-soundpt (Jep's playtest, 2026-10-30: "p1 can hear p2 shooting an enemy from really far away"): the sounds played
// while one lives come from where `at` stands (the partner's step: her swings, shots, hits, items, traps), placed against
// this copy's listener as the game places a monster's (SndGetVolPan), so the other player hears them fade with distance.
// Nests; a SoundOwner's player-only sounds still play on their owner's copy alone.
// DC_COOP_AUDIO_MUTATE=partnerflat (the twin): played flat on every copy, as before.
class SoundFrom {
public:
    explicit SoundFrom(const float *at);
    ~SoundFrom();
    SoundFrom(const SoundFrom &) = delete;
    SoundFrom &operator=(const SoundFrom &) = delete;

private:
    const float *at_;
};

// coop-soundpt: player 2's own weapon about to break (as motionDrive's warning for player 1): its beep on her copy alone,
// on her own clock. Called once a tick on a floor from her step.
void PartnerWeaponWarning();

// coop-atlaopen: player 2's chest and Atla presentations' special sounds (retail's SndSPSePlay: the acquired jingle,
// the Atla's chime), from her own special set on kCoopSpecialPort, so player 1's (the game's MIDI_PORT_SE_SPECIAL set)
// is never swapped. Presentation sound: heard on her copy (and the one copy unlinked), as her presentation is drawn there.
// PartnerSpecialLoad binds set_no ahead of the sound; PartnerSpecialSound plays its se_no (special_se_info's row).
// DC_COOP_AUDIO_MUTATE=specialse (the twin): neither does anything, as before.
void PartnerSpecialLoad(int set_no);
void PartnerSpecialSound(int set_no, int se_no);

} // namespace coop
