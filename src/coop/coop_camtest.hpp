#pragma once

#include "coop/game_ns.hpp"

// coop: cameras
// The in-game checks behind tools\chronicle-win\scenarios\cameras (one per landed camera feature), and
// the switches their mutation twins turn the features off with. All are off unless their test
// variables are set; nothing the game does depends on them otherwise.
//
//   DC_COOP_CAMTEST=NAME      runs check NAME on this copy and logs "camtest: NAME PASS|FAIL ..." once:
//       cameras    each copy draws from its own player's camera, and its player walks the way its stick
//                  points on that camera
//       pullback   the partner, put more than 400 units from the player, stays there
//       intro      on Wise Owl Forest the partner plays its arrival (motion 100) through the opening, and
//                  copy B's opening camera looks at the partner
//       sfx        the partner's footstep, throw, monster-hit and hurt sounds are played
//       wake       a monster only the partner is near wakes, at the same tick on both copies
//       resync     a session reset (CameraResetForSession) at DC_COOP_RESYNC_TICK after the cameras parted,
//                  copy B's camera module coming back as a fresh copy's; the copies must stay in sync
//       lockhud    coop-lockhud: the partner and the player each locked on a monster far from the other; each copy
//                  shows its own player's lock (name, bar, corners) and not the other's far one
//       circlecam  coop-p2circle: no monster lockable, her camera turned 140 degrees from behind her, pad 2 taps
//                  Circle; her camera comes round behind her facing, as retail's does the player's
//       lockpress  coop-lockpress: copy B's own pad holds Circle beside a lockable monster; the lock is taken on the
//                  tick Circle reaches the game, kept through that hold, and a second hold lets go at 24
//   DC_COOP_CAMTEST_END=N     the tick a check gives its verdict by, at the latest
//   DC_COOP_MUTATE=A,B,...    turns features off for the mutation twins: shared_cameras, pullback,
//                             no_view_restore, no_arrival, no_sfx, draw_leak, no_camera_reset

namespace dc::game {
class CCameraFollow;
} // namespace dc::game

// Whether mutation name is on (DC_COOP_MUTATE).
bool CoopMutate(const char *name);

// The checks' per-tick step: CoopStep calls it at its end, every tick.
void CamTestTick();

// coop-p2circle: the pad 2 buttons a check holds (circlecam's Circle tap); CombatInput takes them in place of the
// pad's. False, buttons untouched, when the check holds none.
bool CamTestPad(unsigned short &buttons);

// coop-lockpress: the buttons the lockpress check holds on player 2's own controller; OwnInput adds them to the pad the
// link reads and sends, so they reach both copies through its delay.
void CamTestOwnPad(int player, unsigned short &buttons);

// coop-strafe: pad 2's left stick as the strafe check holds it (DC_COOP_CAMTEST=strafe); Walk calls it after reading the
// pad. Untouched when the check holds none.
void CamTestStick(float &lx, float &ly);

// What DunMainDraw draws from: CoopViewBegin calls it once the camera is chosen; shown is the camera
// the game shows (the player's, or the opening's).
void CamTestView(CCameraFollow *drawn, CCameraFollow *shown);

// The partner's sounds by kind, played where they happen (unless no_sfx), and counted for the sfx check.
enum CoopSound { kCoopSoundThrow, kCoopSoundHit, kCoopSoundHurt };
void CoopPartnerSound(int se_no, float *position, CoopSound kind);

// coop-dungeonp2: CRandomItem::Draw, for each item it draws (the drops check counts its own).
void CamTestDropDrawn(const float *pos);
