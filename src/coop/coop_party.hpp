#pragma once

#include "coop/game_ns.hpp"

// The partner's character: which member it is, its life, its model, weapon and motions, where it is
// put, pad 2's member swap, and lying down and getting up.

#include "collision.hpp"
#include "platform/input.hpp"

namespace dc::game {
class CCharacter;
} // namespace dc::game

namespace coop {

bool BlockEmpty();
bool Pickable(int chara);
int  PickPartner();
// coop-chestsync: the party test hooks (DC_COOP_PARTY, DC_COOP_PARTNER), once per process: at the first floor start
// (NetplayFloorStarted), else at the first pick. apply false spends them unapplied (a copy taking the host's game).
void PartyTestHooks(bool apply);
void StepSwap(const InputPadState &pad);
bool StepDown();
bool OwnLife(int chara);
int  Life();
void SetLife(int life);
int  MaxLife();
void PlayMotion(int motion, int flags);
bool HasMotion(int motion);
// coop-pt1109b (Jep's 2026-11-09 playtest: in a long hall the walls go dark past the draw distance and the far player still draws bright over
// the dark): whether the other player's body is beyond the distance retail draws a monster at (CheckViewLevel's clip_distance, 300, wake and
// draw alike; 310 to go again), seen from this copy's own player. Linked copies on a floor only. DC_COOP_MUTATE=farplayer_drawn (twin): drawn
// at any distance, as before.
bool OtherBeyondClip();
void KeepApart(CCPoly *polys, int count);
// Puts the partner at the player's right hand. Every call logs "coop: P2 placed beside P1 (reason)";
// the default reason is Walk's (fallen off the floor, or the shared camera's leash).
void PlaceBesidePlayer(const char *reason = "walk: fell or leash");
void LoadWeapon(int chara, CCharacter *body = nullptr);
bool ReloadWeapon();
void GrantStandInWeapon(int chara);
// coop-fx51b (tests): DC_COOP_TEST_STANDIN_REGRANT=1, after a state fixture was put back (netplay.cpp's StateFileFrameStart).
void StandInRegrantAfterLoad();
void StandInWeaponStep();
int  StarterCopies(int chara);
void NewFloorCheck();
bool Load(int chara);
// coop-joinvisuals: a floor's state set taken from the other copy: the partner's textures out of the table before the port's
// registry is made again, and her model loaded again after it where the state set stands her (netplay.cpp's apply).
void PartnerSnapshotTexturesOut();
void PartnerMotionTake(); // coop-p1follows
void PartnerSnapshotApplied();
// coop-keydoor: after a join's apply and its texture re-key, the mod's own draw-only texture blocks out of the table, so each
// loads again from this copy's files at its next draw (coop_party.cpp).
void DrawBlocksSnapshotApplied();
// coop-joinvisuals: on the tick, her member's stand-in model loaded for her floor's entrance, or put away after it.
void EntryStandInTick();
// coop-entryshadow test measure (with DC_WORLDS_TEST_ENTRY_SHOT): the entrance stand-in's body and shadow as last posed.
struct EntryStandInPoseInfo {
    int   frame = -1;          // its count among the stand-in's poses, -1 never
    int   motion = -1;         // the body's motion
    float time = 0.0f;         // and its time
    float body_reach = 0.0f;   // widest flat reach of the body's frames from its place
    float shadow_reach = 0.0f; // and of the shadow's
    int   shadow_frames = 0;   // frames in the shadow's tree
    int   matched = 0;         // shadow frames with a body frame of their name
    float gap = -1.0f;         // their mean gap, -1 none matched
};
bool EntryStandInPose(EntryStandInPoseInfo *out);
void PartyResetForSession();
bool StepPresence(bool *rejoined = nullptr);
void PresenceResetForSession();
// coop-localfix: the presence latch (whether player 2 counts as there, and for how many calls she has been absent), carried with a
// join's state set from the host (CoopTownSyncTake / Apply).
void PresenceGet(int *was, int *absent);
void PresenceSet(int was, int absent);
// coop-p2absent: test hook (DC_COOP_PRESENCE_TRACE=1): once a second of calls, where (town or dungeon),
// whether the link is up, whether player 2 counts and whether the partner was drawn on the tick before.
void PresenceTrace(const char *where, bool drawn);

} // namespace coop

#include <filesystem>

// Pad 2's pick beside the save slot (coop-party.dat). memorycardaccess.cpp and the new game call them
// beside the inventory's sidecar.
bool CoopPartySave(const std::filesystem::path &save_directory);
void CoopPartyLoad(const std::filesystem::path &save_directory);
void CoopPartyDelete(const std::filesystem::path &save_directory);
void CoopPartyNewGame();
// coop-lobby: the lobby's locked members (member numbers, -1 none) made the game's; a new game's held until its party is made.
void CoopPartyLobbyPicks(int host, int guest, bool new_game);
