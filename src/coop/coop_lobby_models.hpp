#pragma once

#include "coop/game_ns.hpp"

// coop-lobbymodels (Jep 2026-10-30): the lobby's character boards show each usable
// member's 3D model in its well, drawn in the void with a fixed camera of its own. A plain CCharacter per member, loaded as
// coop_party loads the partner (charaNameTbl's pack, its own arena and texture block) with the save's equipped weapon in
// its hand, or the starting weapon for a member the lobby unlocks (StarterWeapon). Nothing of it is simulation: no world,
// no collision, no actor swap, nothing in a checksum. Idle, a member stands facing the camera with the camera close on its
// upper body; locked, the camera eases out to the whole body over half a second and the owner runs it in place (the run
// turns it to the stick, it never moves) and swings its weapon (Cross: the weapon's attack motions and their sounds).
// Unlocked, its heading and motion ease back to idle facing the camera, and the camera back up, with no snap.
// Log lines start "lobby-models:". DC_LOBBY_MODELS_DEMO=1 draws six wells over the title menu (DemoDraw), each well in
// turn locked, run round and swung, then unlocked; DC_LOBBY_MODELS_DEMO=idle draws them idle only.

namespace coop::lobbymodels {

// Loads members[0..count) (CHARA_TOAN .. CHARA_OSMOND, at most 6; a repeat or a number out of range is left out).
// weapon_items: per member, the weapon item to hold (an ITEM_WEAPON_* of that member), or null / -1 for the save's
// equipped one when the save's status has the member (UserStatus, or SaveData's dungeon status in town and on the title),
// else the member's starting weapon. Open again closes first. False when no member loaded.
bool Open(const int members[], int count, const int weapon_items[] = nullptr);
// Frees the models' textures (their blocks) and forgets them. The model memory is kept for the next Open, zeroed: the
// renderer drops a model's record when its block loses its tag, and reading a freed block would fault. Safe to call twice.
void Close();
// Whether member has a model loaded.
bool Loaded(int member);

// Locking eases the camera out to the whole body (about 0.5 s) and hands the model to Drive. Unlocking eases its heading
// back to the camera, its motion back to idle and the camera back to the upper body over the same time.
void SetLocked(int member, bool locked);
// A member not yet reached in the story (Jep, 2026-10-31: every member's model shows, those as a full silhouette): drawn
// all black, at the well's fade. Off after Open.
void SetSilhouette(int member, bool silhouette);
// Its lights' brightness, 0 (black) .. 1 (full, the default after Open): the lobby's unselected cards drawn darker (Jep,
// 2026-11-01: locked a silhouette, unselected darkened, selected full colour). The fade is kept.
void SetShade(int member, float shade);
// SetShade is here (coop_lobby.cpp calls it only when this is defined, so either branch builds without the other).
#define DC_LOBBY_MODELS_SHADE 1
// The locked model's input for its next step: stick_x, stick_y as Stick() gives them (-1 .. 1, y negative up); the run
// plays and the model turns to the stick's heading in screen terms (up runs away from the camera), its place never
// changes. attack: Cross held; a press starts the equipped weapon's attack. Ignored while unlocked.
void Drive(int member, float stick_x, float stick_y, bool attack);

// Steps member's model one tick and draws it into the rect (x, y, w, h) in the 640x448 2D space the title's menu is laid
// out in, with its own viewport, scissor, depth and camera; alpha 0 .. 0x80 (0x80 opaque) fades it as the title fades its
// own. Call once a frame for each member shown, after the title's backdrop and before the 2D boards; the boards must leave
// the well itself undrawn (the model is drawn in the void). Every renderer setting it changes is put back. step false: a
// second draw of the member this frame (the lobby's slots: two players may show one member), drawn as it stands.
void Draw(int member, float x, float y, float w, float h, int alpha, bool step = true);

// The dev hook (DC_LOBBY_MODELS_DEMO): six wells over the title menu, opened on the first call. DemoClose when the title
// fades out to a pick.
void DemoDraw(int fade);
void DemoClose();

} // namespace coop::lobbymodels
