#pragma once

#include "coop/game_ns.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

#include "types.h"

// The game's own menu beside a running world (Jep's 2026-10-20 ask): in co-op on a dungeon floor, the
// menu Start opens (retail's Triangle menu, BattleMenuCursor and BattleMenuDraw) no longer stops the
// world. Each tick the menu steps on the pad that opened it, then the world steps (MoveChara) with that
// player's character held still and unhurt, so the other player keeps walking and fighting. The menu
// draws over the live scene on the copy of the player who opened it (couch: the one screen); the other
// linked copy steps and draws the menu into a hidden target, so both copies step the same menu state
// (its draw steps some) and stay in sync, and shows only the game.
//
// Player 2's (linked; her Start, which the link hands her port 1 for) shows her member and, with separate bags,
// her bag, while player 1 walks on his pad. On one screen her Start still opens the pausing menu.
// Twins: DC_COOP_MUTATE=fullmenufreeze (the menu stops the world, as retail's), fullmenuview (player 2's menu
// shows player 1's member); DC_NETPLAY_MUTATE=fullmenu_owner (the link takes the menu back from her).

namespace coop {

// Whether the menu is open beside the running world now.
bool FullMenuLive();

// coop-dungeonsuite: whether that menu's pack is still being read (a presentation's own reads must wait for it).
bool FullMenuLoading();

// coop-chestmenu (Jep's 2026-10-28 crash: P1's chest beside P2's menu): the chest, gate key and escape presentations'
// BtCashBuffer base, a block of their own in place of read_buffer (`retail`), where the dungeon menu works. Around their
// reads, PresentationReadsBegin/End keep the open menu's read list. Test hooks (DC_COOP_TEST_CHESTMENU): PresentationFilesTrace,
// the files once read; PresentationFilesCheck each tick of the presentation's loop (built: its models made from them): a
// line when they changed.
u_char *PresentationBuffer(u_char *retail);
void    PresentationReadsBegin();
void    PresentationReadsEnd();
void    PresentationFilesTrace(const void *at, std::size_t size);
void    PresentationFilesCheck(bool built);

// From the dungeon's GAME_TASK_MENU_INIT, once BattleMenuInit has run: the menu opens beside the world
// when co-op allows it.
void FullMenuBegin();

// The dungeon's frame (the port's ModeLoop calls it for GameLoop): with the menu beside the world, the
// menu's step, the world's, and the draws; otherwise GameLoop as before.
int FullMenuGameLoop();

// The player whose menu is open beside the world (1 or 2), else 0.
int FullMenuOwner();
// coop-fullmenu: whether this player has a menu open (the first or the second; both players can, 2026-10-24). coop-localmenu:
// as the step knows it, the player's in-menu bit (PlayerInMenu, set by its open command on every copy alike).
bool FullMenuOpenFor(int player);
// coop-localmenu: whether this copy's menu engine runs that player's menu now (what the open and close commands report).
bool FullMenuRunsFor(int player);
// coop-pt1111n: inside her menu's view (UserStatus->cur_chara is her member) player 1's member, which the view kept; else -1.
int FullMenuViewPlayerChara();
// coop-localmenu stage 3a: the game menu on its player's copy alone (coop_fullmenu.cpp says how).
// Whether this copy runs that player's menu as a local one.
bool FullMenuRunsLocalFor(int player);
// From retail's menu opening (movechara.cpp, a recipe), on every copy alike: true when it took the opening (the world enters
// no menu task); false leaves it to retail's tasks, as before.
bool FullMenuOpenLocal();
// Whether menus run local (linked, the menus on): Route moves no port on a menu's Triangle then.
bool FullMenuLocalOn();
bool FullMenuCarryOn(); // coop-small2: local menus as a merge carries one (no linked partner yet)
// coop-soakfix4: local menus on but for the partner being linked (a catch-up under way counts: the link is up).
bool FullMenuLocalOnLinkUp();
// coop-menulocal: the pad button (kInput*) that opens a local menu: Triangle, Start with game.coop_menu_panel on.
std::uint16_t FullMenuLocalButton();
// The map jump that player's closed local menu left (the close command carries it).
int FullMenuCloseJump(int player);
// The close command applied (every copy, its link tick): retail's close task runs with the map jump.
void FullMenuClosedByCommand(int player, int jump);
// coop-stage4: a town's local menu (coop_townmenu) writes the party status as the floor's does: around its step on its owner's copy
// (inside the owner's view), Begin(true) lays this copy's unapplied pokes over the live status and End sends each word the step
// changed as kCmdPoke and puts the live status back; Begin(false) around its draw lays the overlay only.
void FullMenuWritesBegin(bool collect);
void FullMenuWritesEnd();
// The first menu shut while the second stays open (its owner walks again).
bool FullMenuFirstClosed();

// Whether player 2 has the menu open beside the world (her character stands, kept unhurt).
bool FullMenuHoldsPartner();

// BattleMenuDraw: whether to leave out the still frame it draws behind the menu (the world is live).
bool FullMenuLiveScene();

// coop-menuflash: the dungeon's MainDraw: whether this copy draws the mini-map that the other player's menu (opening
// or open) hid on both copies.
bool FullMenuMapKept();

// coop-hudblink: the dungeon's MainDraw: the HUD's slide (rogoY3) this copy draws, so that only its own player's menu
// (opening or open) hides it.
int FullMenuHudY(int y);

// Session reset: closed.
void FullMenuResetForSession();

// coop-quitown (Jep's note 9, 2026-10-25: "P2 closed P1s game when hitting quit from menu"): the menu's Quit Game runs
// on both linked copies (each steps every menu on the agreed pads), so only the copy of the player whose menu it is
// quits; the other closes the confirm and plays on, and hears the Leave. The menu being stepped is the one its caller
// names (FullMenuStepping around BattleMenuCursor: the menus beside the world, her town menu), else the town's own
// (player 1's) or the link's menu owner's. True when not linked. DC_COOP_MUTATE=quitboth (twin): both copies quit.
void FullMenuStepping(int player);
// coop-localmenu 3c: a member picked in that player's local menu (the Characters page, MenuCharaChange) goes to every copy as a
// command (kCmdPick) when this copy runs it; true when it went (the caller then changes nothing here).
bool FullMenuLocalPick(int player, int wanted);
// coop-pt1111k test hook: that pick taken on this tick, as its command is on every copy.
void FullMenuApplyPickForTest(int player, int wanted);
bool FullMenuQuitHere();
// coop-quittitle (Jep's rule, 2026-11-10): the menu owner's Quit Game (Yes), on its copy, in a netplay session: the quit goes to
// every copy as a command (kCmdQuit) and the title return comes on the tick it applies (netplay.cpp ApplyQuit,
// NetplayTakeQuitToTitle), not at once here. True when it went (the caller then sets no title request of its own); false for
// single player (upstream's own return to the title).
bool FullMenuQuitSent();

// coop-townmenus: the second menu's means, for the town's (coop_townmenu): every global the game's menu keeps, copied
// aside and back (MenuStateBytes bytes); the second menu's own texture blocks; its work buffer (zeroed by each call).
void        MenuStateSave(std::vector<unsigned char> &to);
void        MenuStateLoad(const std::vector<unsigned char> &from);
std::size_t MenuStateBytes();
const int  *MenuSecondBlocks(int *count);
u_long128  *MenuSecondBuffer();

// coop-invcrash (Jep's 2026-10-27 crash, both players in their menus): DngWeaponEquipModelBuild, the weapons page's way
// out, about to rebuild the weapon models from the .chr at `file`. With DC_COOP_MENUDEBUG, logs whose menu is stepping and
// whose read buffer the file lies in (a menu's own, or the other menu's).
void MenuWeaponBuildTrace(const void *file);

// coop-herweapon (Jep's 2026-10-28 crash, both copies, at Toan's first swing after P2's menu): whether the weapons page's way
// out (DngWeaponEquipModelBuild) may build chara's weapon into the world's player (MainWeapon). Not while player 2's menu
// shows her member: her page's chara is hers, and her hand's model follows the save by itself (coop_combat StepThirst).
// DC_COOP_MUTATE=herweapon_world (twin): it may, as before (Toan then held her slingshot, and his swing crashed).
bool MenuBuildsWorldWeapon(int chara);

// coop-worlds: the pair splits and this copy stays on the floor. The menu open beside the world (the other
// player's, who left with it open, Jep's 2026-10-24) ends with the menu's own way out (ExitBattleMenu), and the
// pad leaves the menu's stick mode, or this copy's player walks with the stick stepping the quick-use slot.
// DC_COOP_MUTATE=staymenu (its twin): both stay, as before.
void FullMenuEndForSplit();
// coop-pt1106-lockup: the sender's own retail floor menu goes on as a local menu as a take comes due (coop_fullmenu.cpp).
bool FullMenuRetailToLocal(int stayer = 0);
// coop-soakfix4: the menu beside the world (g_full), opened during a catch-up, goes on as a local menu as the lockstep starts.
bool FullMenuFullToLocal();
// coop-repinfix2: and one opened during a catch-up before the take, as the take comes due on a floor (soak leave 2239).
bool FullMenuFullAtTake();
// coop-pt1106-lockup: a local menu's command handlers, set once on every copy (coop_actor.cpp ActorMenuSync).
void FullMenuCommandHandlers();

// coop-fx51a (a warm pair's 32-row run, 2026-11-05: p2write ends on its write's pokes with player 2's menu still open, and
// the next job's fixture load put back the game's state under a menu engine still live, which stepped a null CUserStatus,
// copy B 0xc0000005 at CUserStatus::Step): a job's start ends any menu open beside the world the way the split does, then
// sets the menus' own state as a new session's, as the fixture's free play had them. DC_COOP_MUTATE=job_menu_kept
// (warm-repeat-selftest -MenuTwin): left as the last job left them.
void FullMenuEndForJob();

// coop-menudepth: a menu about to draw over the world's frame on owner's copy clears the world's depth first, as retail's
// menu frames have none, so the menu's 3D (the Weapons page's models) is not hidden by the world. Call it right before its
// BattleMenuDraw on the copy that shows it.
void MenuOverWorldDepth(int owner);

// coop-stabfix2 (soak seed 2074, floor: OUT OF SYNC at 540 on view, then stages): the menus' camera (MenuCamera, menuetc) is one
// global every menu's draw reads (MenuWorldTrans: the render info's view, which the checksum sums and the next tick's clip tests
// and walk read), and its pages set its eye and look-at (the Weapons page's models: 0,0,60 to 0,0,-45). An engine on one copy
// alone (a local menu, a town engine) set them there alone, so a later lockstep menu (his Triangle in the first-person view)
// drew from them on that copy and from the untouched camera (eye on look-at: a null view) on the other. Such an engine keeps
// its own camera, swapped in around its step and draw as its pad is; it starts as the world's. DC_COOP_MUTATE=
// localmenu_camera_here (twin): the engine's camera is the world's, as before.
struct MenuCameraOwn {
    float pos[4] = {};
    float ref[4] = {};
    float next_pos[4] = {};
    float next_ref[4] = {};
    float roll = 0.0f;
    s32   limit_move = 0;
    float speed = 0.0f;
    float angle_h = 0.0f;
    float angle_v = 0.0f;
    float snap_range = 0.0f;
    bool  set = false; // taken from the world's at its first swap
};
// The engine's camera in place of the world's (the world's then held in `own`), or back: each call swaps.
void MenuCameraSwap(MenuCameraOwn &own);

// coop-offthread (the shared-state review's option 4): the battle menu's working globals against the state set: how many of
// them it holds (only the 16 the world reads, since their removal) and a hash of the others' bytes on this copy.
void MenuWorkCensus(std::size_t *held, std::size_t *total, std::uint32_t *own_hash);
void MenuWorkRegionsAll(void (*add)(void *at, std::size_t size, void *context), void *context);
// (DC_COOP_MUTATE=merge_menu_retail or scriptmenu_merge_retail, a retail menu stepped in the lockstep through a merge: their twins
// need the menu state carried, as before)
bool MenuRetailLockstepMutated();
// coop-pt1111m: the menus are retail's, in the lockstep (game.coop_menus off): their working globals are world state, in the set
bool MenuWorkInSet();
bool          MenuWorkKeptMutated(); // (DC_COOP_MUTATE=menusoff_menu_out, the twin: a link's apply keeps this copy's own)
void          MenuWorkSave(std::vector<unsigned char> &out);
void          MenuWorkRestore(const std::vector<unsigned char> &in);
std::uint32_t MenuWorkHash();

} // namespace coop

// coop-apartsync: whether this copy writes the save its save screen makes now (CMemoryCardAccess::SaveToMc). Linked, both
// copies step every menu, so the save screen's write ran on both PCs and his save landed in her save folder too: only the
// owner's copy writes (the menu as coop::FullMenuQuitHere names it). A joiner replaying the host's play (a catch-up) writes
// none. True when not linked. DC_COOP_MUTATE=save_both (twin): both copies write, as before.
bool FullMenuSaveHere();
