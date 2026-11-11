#pragma once

#include "coop/game_ns.hpp"

// The partner's swings and shots, the monsters' hits on it, and pad 2's combat buttons: charging,
// lock-on, aiming, weapons and items.
//
// Pad 2 (fixed buttons, so linked copies agree whatever each one's OK/NO setting):
//   Cross       tap swings (or opens a door or chest); held past the swing it charges, and letting go
//               lands a charged blow or shot (one and a half times the damage); the second level,
//               which a character reaches once it has its event skill, adds a wave as strong round
//               the partner, as the player's does.
//   Circle      tap locks onto the nearest awake monster in front, or the next one when locked; held
//               for 24 ticks lets go. Locked, the partner faces the monster and its shots fly at it.
//   R2          held plants the partner's feet: the left stick turns it and tilts the aim, a line of
//               dots shows where a shot goes, and Cross fires.
//   Square      uses the item picked from the shared pack: food heals the partner, bombs, stones and
//               fruit are thrown ahead (or at the locked monster).
//   D-pad       Left and Right pick the item, Up and Down the partner's weapon.
//   Triangle    opens the partner's panel under its life bar (weapons and items; the game does not
//               pause): Up and Down move, Left and Right change page, Cross equips or uses,
//               Triangle or Circle closes. The partner stands still while it is open.
//
// Everything here moves only with the two pads and the game's state, so linked copies keep step.

#include <cstdint>
#include <vector>

#include "platform/input.hpp"
#include "healeffect.hpp"

namespace dc::game {
class CCharacter;

struct WEAPON_HAVE;
} // namespace dc::game

namespace coop {

constexpr int kThrows = 2;

// A thrown item in flight: where it is, its velocity, the item and the ticks it has left.
struct Throw {
    float pos[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float vector[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    int   item = -1;
    int   life = 0;

    bool operator==(const Throw &) const = default;
};

struct Combat {
    // Pad 2's buttons last tick, for presses and releases.
    std::uint16_t buttons = 0;

    // Cross is held this tick; Cross has been held since the swing began.
    bool cross_down = false;
    bool held = false;

    // The charge under way: whether the swing is held at its charge, ticks charged, the level reached
    // (0 to 2), and the damage scale of the blow or shot it releases.
    bool  charging = false;
    int   charge_ticks = 0;
    int   charge_level = 0;
    bool  charged_blow = false;
    float scale = 1.0f;
    int   charges = 0;

    // The partner's "speed" gauge (0 to 100) as the game keeps the player's (BtActStatus.action_gauge):
    // a tap's swing costs none of it, a charge empties it, and it fills back at its weapon's speed
    // (StepGauge). The bottom-left panel shows it on the copy whose player is P2.
    float gauge = 100.0f;

    // coop-swapfix: the partner's guard, as the player's (DunMoveChara, action_on 6): R1 held while
    // locked on raises it (motion 8), holds it (9) and lowers it once let go (10); 0 when not guarding.
    // Blows (hit kind 3) that land while it holds are blocked; blocks counts them.
    int guard = 0;
    int guard_ticks = 0;
    int blocks = 0;

    // The monster locked onto, or -1, and how long Circle has been held.
    int lock = -1;
    int circle_ticks = 0;

    // R2 held: the partner's aim, a yaw and a tilt (up positive).
    bool  aiming = false;
    float aim_yaw = 0.0f;
    float aim_pitch = 0.0f;

    // The item Square uses (an item number, -1 for none) and ticks before the next.
    int item = -1;
    int item_wait = 0;

    // coop-p2feather: her Square held with a Dran's Feather selected, her run boosted this tick.
    bool boost = false;

    // coop-p2throw: her quick-slot throw as retail's (BattleActionThlow): 0 none, 1 aiming (Square held, motion 26, the landing
    // point walked with the stick), 2 throwing (motion 27 up to the release frame), 3 the throw's tail. Locked on, it starts at 2.
    int   throw_stage = 0;
    int   throw_item = -1;
    int   throw_ticks = 0;
    bool  throw_locked = false;
    float throw_aim[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float stick_x = 0.0f;
    float stick_y = 0.0f;

    // The panel: open, its page (0 weapons, 1 items) and row.
    bool panel = false;
    int  page = 0;
    int  row = 0;

    // Each shot's damage scale, beside the shots it scales.
    float shot_scale[4] = {1.0f, 1.0f, 1.0f, 1.0f};

    Throw throws[kThrows];

    // The member the partner was last tick: a swap drops what the last one was doing.
    int chara = -1;

    // Pad 2's wheel was open last tick.
    bool wheel = false;

    // Ticks before healing water heals the partner again.
    int heal_wait = 0;

    // Monsters whose kill StepKills has credited (until alive again); kills credited and the last one's ABS.
    bool credited[16] = {};
    int  kills = 0;
    int  last_kill_exp = 0;

    // The weapon (item and slot) the partner's hand was last seen with (-2 for not yet), wear and
    // breaks taken, and ticks toward a healing weapon's next point of life.
    int held_item = -2;
    int held_slot = -2;
    int wear_ticks = 0;
    int breaks = 0;
    int regen_ticks = 0;

    // Ticks toward the partner's poison's next bite.
    int poison_ticks = 0;

    // coop-p2hurt: the hurt motion a blow it lived through plays (6 the flinch, 4 the knockdown; 0 none) and the
    // knock-back it pushes it with, as BtCheckDamageProc's action_on 5/4 and blowVelo for the player; hurts counts them.
    int   hurt = 0;
    float blow[3] = {0.0f, 0.0f, 0.0f};
    int   hurts = 0;

    // The turn of Osmond's propeller.
    float propeller = 0.0f;

    // The player down while the partner plays on: ticks left before it gets up, and how long the
    // partner has stood still beside it.
    int player_down = 0;
    int player_revive = 0;

    // Counts the automated tests (coop_combat_test.cpp) read: blows and shots made and the last one's
    // damage, charge waves, shots that struck a monster, thrown items landed, hand models reloaded.
    int blows = 0;
    int last_blow_damage = 0;
    int waves = 0;
    int last_wave_damage = 0;
    int shots_hit = 0;
    int throws_landed = 0;
    int reloads = 0;

    // Every field, for the reset test's check against a fresh boot.
    bool operator==(const Combat &) const = default;
};

extern Combat g_combat;

// The partner's equipped weapon, or null when its slot holds none (a stand-in that has not joined has
// slot -1).
const WEAPON_HAVE *EquippedWeapon(int chara);

// A blow's damage before the charge, from the member's own weapon (or its starting one), and the
// defence a hit on it is reduced by; the member that has a weapon when it has not joined starts with.
int  BaseAttack(int chara);
int  Guard(int chara);
int  StarterWeapon(int chara);
bool Stamina(int chara);

// The test bot (DC_COOP_TEST=name, coop_combat_test.cpp): CombatTestTick runs it once a tick from
// DunMoveChara, CombatTestPad puts the pad 2 buttons it holds into pad. Both do nothing unless set.
void CombatTestTick();
bool CombatTestPad(InputPadState &pad);

// Puts all of combat's state (g_combat, the floor it last saw, whom each monster goes after) back as a
// fresh boot has it. CoopResetForSession calls it when a linked session starts over.
void CombatResetForSession();

// The partner's "weapon ready" gauge (0 to 100), as the copy whose player is P2 shows it in the
// bottom-left panel: full when it can swing, filling through a swing and through a charge (full at the
// level its character can reach). Read only.
int PartnerGauge();

// Fills the partner's gauge back one tick, as MotionDrive fills the player's; Walk calls it each tick.
void StepGauge();

// What CoopDrawWeaponPanel last drew (for the tests; draw-side only, nothing in the tick reads it but
// the test bot): whether it drew the panel in place of the player's, the weapon, the gauge, and
// whether the weapon's own icon could be drawn, and the sheet it was copied from (1 itemlst.img's,
// which shares the HUD's palette; 2 allicon.img's; 0 none).
struct WeaponPanelShown {
    bool drawn = false;
    int  item = -1;
    int  gauge = -1;
    bool icon = false;
    int  icon_item = -1;
    int  icon_sheet = 0;
    int  frames = 0;
};

extern WeaponPanelShown g_weapon_panel_shown;

// What CoopTopStatusInfo last drew in P2's place (tests only): whose, its life and weapon line.
struct TopStatusShown {
    bool drawn = false;
    int  chara = -1;
    int  life = 0;
    int  life_max = 0;
    int   whp = 0;
    int   whp_max = 0;
    int   item = -1;
    float water = 0.0f;
};

extern TopStatusShown g_top_status_shown;

// Whether this copy's own player is P2 (linked copy B) with P2 on the floor.
bool LocalIsPartner();

// Ruby's ring magic for the partner: ring shots launched, how many fly now, and the element loaded
// (-1 for none).
int RingShots(int *active, int *element);

// Osmond's muzzle flashes set off for the partner, and whether his gun's own effect is the one loaded.
int MuzzleFlashes(bool *loaded_osmond);

// The texture block P2's effect (Ruby's ring, Osmond's muzzle flash) is loaded into.
int PartnerEffectBlock();
// coop-osmondflash: that block's textures as the effect loaded them (the effectblock test's reference).
const std::vector<u_long> &PartnerEffectLoaded();

// Whether that state is as a fresh boot has it; why says what is not.
bool CombatIsFresh(char *why, int size);

int  Damage();

// The partner's own ailments (its member's in the save), set as BtSetStatusErr sets the player's,
// and whether it has one.
void PartnerSetAilment(int status);
bool PartnerAilment(int status);

// Event scripts 0xF and 0x10 (a trapped chest) are the partner's: their bomb and ailment go to it.
extern bool g_trap_by_partner;
void        TrapOpenedByPartner();

// coop: gaps2
// A trap circle (CDungeonMap's trap_circle) the partner steps on springs for the partner, as
// DistTrapCircle and Run_TrapCircle do for the player: its own stamina, its own weapon's change, its
// purse, or all the monsters' stamina. Walk calls it each tick of play.
void PartnerTrapCircle();

// coop: gaps2
// Whether the partner sits out this floor: only one member may fight on it (res_limit_zone_current 0
// to 5) and the partner is not that member. Its swing, shot, aim and items are then refused.
bool SitsOutFightZone();

// The player's own down while the partner is up (Jep: eject only when every player is down):
// PlayerGoesDown takes the player down instead of the game's death and is true when it did;
// PlayerDownStep runs it each tick (0 not down, 1 down, 2 both now down: the game's death runs);
// BothDown is whether the partner is down too, so the game's death goes to its game over.
bool PlayerGoesDown();
int  PlayerDownStep();
// Whether the player lies down now (it neither walks nor turns; DC_COOP_MUTATE=p1spin turns it, the twin).
bool PlayerLiesDown();
bool BothDown();

// Whether owner (a hit's) is the partner's: its character, on the floor, not the controlled one's.
bool PartnerHit(int owner);

// Wears the partner's weapon as BattleSubWeaponDmg wears the player's (break, Auto Repair Powder and
// all); 1 or 2 when it broke, as that returns.
int PartnerWeaponWear(float amount, int kind);
// Gives one of the partner's hit records its weapon's element and monster bonuses.
void MarkHit(int hit);
bool Ranged(int chara);
void StartStage(int stage);
// coop-xiaostamina: whether the partner may start an attack now (Xiao's and Ruby's shot waits for a
// full gauge, which StartAttack then spends).
bool AttackReady();
void StartAttack();
void Shoot();
void StepAttack();
void StepShots();
void TakeHits();
void BeginHurt(int hurt, int kind); // coop-p2hurt: her hurt motion from g_combat.blow

// coop-p2hurt: the partner at no life lies down (Jep's down rule): its play stops and the dead motion holds. A blow,
// thirst and poison (Jep's 2026-10-28 playtest: P2 poisoned to 0 showed near death but was never down) all come here.
// why names the cause in the log.
void PartnerFalls(const char *why);

// coop-p2hurt: a blow it lived through plays its hurt motion to its end (6 the flinch, 4 the knockdown) and holds its
// play meanwhile, as action_on 5/4 hold the player's. True while it plays; Walk adds HurtPush's knock-back to the walk.
bool StepHurt();
void HurtPush(float *velocity);

// Reads pad 2's combat buttons for the tick, cross_pressed being Cross's press. Returns true when
// the partner stands still this tick: swinging, charging, aiming or in its panel.
bool CombatInput(const InputPadState &pad, bool cross_pressed);

// The partner's guard for the tick (R1 held while locked on); true while it guards.
bool StepGuard(std::uint16_t buttons);

// Drops pad 2's charge, aim and panel while something else (the member wheel) has pad 2, and takes
// its held buttons as already pressed, so none fires when pad 2 comes back.
void CombatCancel(std::uint16_t buttons);

// coop-strafe: whether the partner walks locked on this tick, as the player does with a lock (DunMoveChara's lock-on
// branch): locked, not aiming, not down. DC_COOP_MUTATE=strafe (twin): never (she ran at the run's pace and motions).
bool Strafing();

// coop-p2feather (Jep's 2026-11-04 playtest: the cheated Dran's Feather did nothing for P2): the Dran's Feather is retail's running item
// (ACTIVE_ITEM_FEATHER, DunMoveChara): Square held on the floor runs at 3.0 for 1.45, a use spent each frame. True while her Square
// holds the boost this tick. DC_COOP_MUTATE=p2feather (twin): never, as before.
bool SpeedBoosted();

// coop-strafe: the partner's motion while Strafing, from the way she steps (move_x, move_z, her camera's) against the
// way she faces the monster; walk when the model lacks it.
int StrafeMotion(float move_x, float move_z, int walk);

// Turns the partner to the monster it is locked onto, or the way it aims. Walk calls it after the
// stick has turned it.
void CombatFace();

// Turns Osmond's propeller on chara's model one tick on (angle is that model's).
void SpinPropeller(CCharacter *chara, float *angle);

// The propeller's turn as drawn (its world x axis in its parent's), -9 without one.
float PropellerTurn(CCharacter *chara);

// The partner's usable items in the shared pack, in pack order, each once; returns how many.
int UsableItems(int *out, int max);
int ItemCount(int item);

// The partner's weapon slots that hold a weapon; returns how many.
int WeaponSlots(int *out, int max);

// The partner's heal sparkle (HealingWater's, its own) and where it was last drawn; the heal the
// partner takes standing in a heal spring or healing water, which sets it off.
extern CHealEffect g_heal;
extern float       g_heal_drawn_at[3];
void               PartnerSpringHeal();

} // namespace coop

// On the linked copy whose player is P2, draws the bottom-left weapon panel for P2 (its weapon's icon
// and its own gauge) in place of the controlled character's, and returns true; otherwise draws nothing
// and returns false, and the game draws its own. alpha is the panel's. DunMainDraw calls it.
bool CoopDrawWeaponPanel(int alpha);

// topStatusInfo for the player this copy plays (P2's life and weapon line on linked copy B).
// DunMainDraw calls it in place of topStatusInfo.
void CoopTopStatusInfo(int y, int selected_item, int floor);

// coop-p2hurt (Jep's 2026-10-28 playtest: P1 poisoned, P2's screen showed the poison icon top right): BtStatusErrDraw for the
// player this copy plays, as CoopTopStatusInfo: on linked copy B her member's ailments, not the controlled character's. Draw only.
// DC_COOP_MUTATE=errdraw_player (twin): the controlled character's on both, as before.
void CoopStatusErrDraw(int y);

// Forgets whom each monster goes after (coop_target.cpp); CombatResetForSession calls it.
void CoopTargetReset();
