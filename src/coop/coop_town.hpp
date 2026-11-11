#pragma once

#include "coop/game_ns.hpp"

#include <cstddef>
#include <cstdint>

namespace dc::game {
class CCameraFollow;
class CCharacter;
class CCamera;
} // namespace dc::game

// Player 2 in town (game.coop): pad 2's left stick walks the partner beside Toan in the towns, the way
// the game walks Toan (EdMoveChara: the town camera's angle, the ground's or the interior's collision).
// The partner only walks: it is stepped while the town is in plain walking (never in Georama's build
// mode, a menu, a talk, an event, a door or fishing), reads only pad 2's stick (Georama's R3 pass and
// everything else stay where they are), and is kept near Toan, so it cannot talk, open doors or leave
// town. Toan stays the town's player. Both linked copies step it from the shared pad.

// Steps the partner. The town's walking mode calls it each tick after MainMode.
void CoopTownStep();

// Draws the partner. EditMainDraw calls it after the town's characters (and the interior's MainDraw after Toan).
void CoopTownDraw();

// coop-fixtures (tests): the link tick DC_COOP_DOORPROMPT's cave door test armed on, -1 before (the press clock of
// DC_COOP_MENUPRESS_FROM_DOOR counts from it).
long long CoopTownDoorPromptArmed();

// coop-inbuild: the partner inside a building (Jep's 2026-10-24 playtest: both in the village head's house, Xiao was gone
// and could not move). EditInLoop calls it each tick, both copies alike: the room's parts her walk collides with, its
// camera (her own camera's start), whether it has fixed camera markers, the room's mode (its debug line), and whether she
// steps this tick (not while Toan's door takes him out, where she stands). Pad 2 walks her as outside; linked, her own camera follows her
// through the room's markers and her Cross at a door takes her out (CoopWorldsLeaveBuildingBy). True when her door takes
// this copy out now. DC_COOP_MUTATE=no_indoor_partner (twin): no partner indoors, as before.
namespace dc::game {
class CMapParts;
} // namespace dc::game
bool CoopTownInteriorStep(CMapParts *parts, int parts_count, CCameraFollow *room, bool markers, int mode, bool steps);
// coop-p2collide: the room's walk, once Toan's own walk has started nothing this tick: the event her Cross starts there (a
// scripted pickup of the room, such as the mayor's closet; program: the room's script), or -1.
int  CoopTownHerRoomEvent(const void *program, int room_mode);
// coop-roomevent: her room event is hers. Around the room's start of the event her Cross started (EdEventInit): In makes her
// body the script's main character (till the event is over) and her pad and bag the game's (false: the twin, Toan's as
// before); InitOut puts the pad and bag back (started: the room went into its event mode on it). Each event tick, around
// EdEventMode: StepIn and StepOut, the same. End, on the room's tick the event ended on (after its characters' step): the
// main character back to Toan, her walk on from where it left her. On: her event runs (both copies alike).
bool CoopTownHerRoomEventIn(int event);
void CoopTownHerRoomEventInitOut(bool started, bool talk);
// coop-hertalk: her Cross at a villager (no pickup in reach) sets its talk up and runs its talk script as her room event; its
// talk return makes the room's talk mode hers (FromEvent, or Begin from InitOut when the script goes to talk at once): edit_in
// runs EdTalkMode with her body (TalkBody) between StepIn (her pad and bag; her copy's view her camera's) and StepOut, Toan
// walking, and End when it returns. Her talk's window is drawn out of sight on his copy (CoopTownHisTalkHiddenBegin).
bool        CoopTownHerTalkFromEvent(int result);
// coop-talks2: her talk in her own talk state and window beside the room's: started by edit_in between StartIn and StartOut
// (its talk script run at once in her state; talks: it went to the talk mode, runs: EdEventInit's result), swapped in to
// draw (DrawIn/Out, HerTalkHiddenBegin: out of sight on his copy). Pending: her Cross found a villager this tick.
void CoopTownHerTalkStartIn();
void CoopTownHerTalkStartOut(bool talks, int runs);
bool CoopTownHerTalkPending();
bool CoopTownHisTalkWaits();
bool CoopTownTalkTarget(int villager); // coop-playfix: the villager this screen's player could talk to (its name, red cursor)
void        CoopTownHerTalkBegin();
bool        CoopTownHerTalkOn();
bool        CoopTownOwnEventIsOthers(); // coop-pt1108f: the running own town event is another player's (this copy's player walks on, its clock shown)
int         CoopTownEventPadPlayer();   // coop-scriptmenus: the player whose pad the running event reads (its script's menus are theirs)
bool        CoopTownEyeOnThisScreen(); // coop-pt1108f: the town's first-person look (retail's viewMode, his) is what this copy's screen shows
CCharacter *CoopTownHerTalkBody();
// coop-pt1109b: the camera her outdoor talk moves in (retail's TalkCamera offsets, EdTalkMode's camera argument): her town camera while it
// runs outdoors; null indoors (retail's room talk has none) and under DC_COOP_MUTATE=hertalk_nocam (twin).
CCameraFollow *CoopTownHerTalkCamera();
// opt8: the villagers' walk holds only while every player talks or is in a menu (Jep 11-09); each villager stops for the nearest player.
bool  CoopTownVillagersHeld();
float CoopTownNearestPlayer(const float *villager, float from_game_player);
// opt8 (Jep 11-09: one talk path for every player): each player's talk from a slot of its own (coop_town.cpp).
bool CoopTalkOn(int index);
bool CoopTalkAny();
bool CoopTalkLocal();        // this copy's own player talks
bool CoopTalkHoldsWalker();  // the game's player talks from its slot: its walk stands, its menu, pause and Georama stay shut
bool CoopTalkWorldHeld();    // every present player in a talk or a menu
void CoopTalkIntoSlots();    // once a tick before the town's or room's mode switch
int  CoopTalkStep(int index, CCharacter *resident, CCamera *room, CCameraFollow *camera, int view_mode, int *choice);
int  CoopTalkOver(int index, int result, int choice); // the result for retail's rule (the game's player's), else 0
bool CoopTalkDrawBegin(int index);
void CoopTalkDrawEnd(int index, bool hidden);
bool CoopTalkHudHiddenBegin();
void CoopTalkCameraTrace(float angle, float distance, bool ends);
bool CoopTalkShadowsOff(); // opt8: retail's talk mode's no-shadow look on a talker's own screen
void CoopTalkHudHiddenEnd(bool hidden);
unsigned char *CoopTownTalksBytes(std::size_t *size); // the snapshot extra
void        CoopTownHerTalkStepIn(CCamera *room);
void        CoopTownHerTalkStepOut();
int         CoopTownTestTalkShop(int result); // coop-p1parts: test hook DC_COOP_TEST_HERTALK_SHOP
void        CoopTownHerTalkEnd(int result, int event_no);
bool CoopTownHerRoomEventOn();
// coop-closetglow: the room's key mode for this step (edit_in.cpp's EditInLoop): retail's lock while a message is up or the bag
// overflows, but not on the room's walk for a message that is player 2's.
int CoopTownRoomKeyMode(bool message, bool overflow);
void CoopTownHerRoomEventStepIn();
void CoopTownHerRoomEventStepOut();
void CoopTownHerRoomEventEnd(int result);
// The camera her event starts from (her own; room: the room's, without hers).
namespace dc::game {
class CCamera;
} // namespace dc::game
CCamera *CoopTownHerRoomEventCamera(CCamera *room);
// Toan's draw during her event: the event's info with its player drawn (he walks), else event as given.
namespace dc::game {
struct ED_EVENT_INFO;
} // namespace dc::game
const ED_EVENT_INFO *CoopTownRoomEventForToan(const ED_EVENT_INFO *event);
// Around a picture of her event the room draws (sprites, item, windows, fade): his copy draws it out of sight (they step as
// they draw). frame_end: the frame's last (counted for the log).
bool CoopTownRoomEventHiddenBegin();
void CoopTownRoomEventHiddenEnd(bool hidden, bool frame_end);
// Her event's fade (the room's last draw): from her event on till the screen has cleared after it, then as HiddenBegin.
bool CoopTownRoomFadeHiddenBegin();
// In a Georama frame (which draws no characters): the other player walking, as the builder sees them (Jep, 2026-10-24).
// EditMainDraw calls it in its Georama modes.
void CoopGeoramaDrawWalker();
// Player 2's copy leaves Toan out of its frame while player 1 builds (her body goes while she builds, on both).
bool CoopTownHidesToan();

// coop-worlds: player 2's copy apart draws her member in Toan's place this tick (Toan is then not drawn).
bool CoopTownSkinsToan();
// coop-p2skin: the same skin inside a building (EditInLoop calls it each tick, before its frame): player 2's copy apart
// there draws her member in Toan's place too. Clears the town partner otherwise (no partner indoors).
void CoopTownInteriorStep();
// DC_COOP_DEBUG: each indoor frame's character drawn on player 2's copy (her skin or Toan), counted for the tests.
void CoopTownInteriorDrawn(bool skin);
// coop-p2doors: each tick of a door opening in town: player 2's copy apart (the door she took) steps her skin there.
void CoopTownDoorOpenStep();
// coop-bagapart: around the town's own menu (edit.cpp's init, step and draw of its battle menu): on player 2's copy apart her
// member is the party's current one there, so the menu shows her beside her bag (the game's player is Toan, her skin on him).
void CoopTownApartMenuBegin();
void CoopTownApartMenuEnd();

namespace coop {

// coop-worlds: Toan stands where the partner stood (player 2's copy staying in town); its place in out.
// False when the partner was not in town.
bool TownPlacePlayerAtPartner(float *out);

// coop-town, player 2's own Georama: its camera starts from hers (TownCamPose); she leaves it where its cursor
// stood (TownPlacePartner).
void TownCamPose(CCameraFollow &to);
void TownPlacePartner(const float *at);

// Session reset (CoopResetForSession): nobody loaded; the next town tick loads the partner again.
void TownResetForSession();

// coop-housemerge: the pair merged inside a building (both copies, the merge's tick): player 2's character goes into the
// room afresh on her next step there (PlaceInRoom, from Toan's pose), so both copies have her where they both would.
void TownMergedIndoors();
// coop-localfix: a merge outdoors by the host's snapshot, on both copies on its tick: where she stands (the stand, a step aside
// when Toan is on it), kept by the host's town load after.
void TownMergedOutdoors();
// coop-exitscene: the pair split: a merge's place not used by then is no place for a later load (both copies).
void TownMergePlaceDrop();

// Whether the town partner is shown, and if so where it stands and which member it is (either may
// be null). Pad 2's wheel reads it in town.
bool TownPartnerShown(float *pos, int *member);

// Whether pad 2 may pick chara in town (never Toan; a member who has joined, or any in a party of one).
bool TownMemberPickable(int chara);

// The member player 2 plays (her pick, if that member has joined, else Xiao): coop-p2entry, her copy apart on a floor.
int TownPlayerMember();

} // namespace coop

// Player 2's own copy of a linked game looks at player 2 in town: the view (only the drawing; the
// town's camera, which Toan's walking is relative to, is left alone) is carried by the partner's offset
// from Toan. The town's frame calls it before it sets the view; walking is whether the town walks.
void CoopTownView(float (*view)[4], float *eye, bool walking);

// coop-soundpt: the town's sound listener (eye, dir; editloop's EdSetSoundSrcVol) from player 2's own camera on her copy.
void CoopTownListener(float *eye, float *dir);
void CoopTownListenerTick(); // coop-pt1108

// Player 1's pause and Georama are his own in a linked town (Jep, 2026-10-20): player 2 walks on through them.
// CoopTownStepAside steps the partner, on both copies, after the town's mode step while the town is paused from
// walking or in Georama (its build mode, parts menu and pause). CoopTownDrawnMode is the mode player 2's copy draws
// the frame in: walking for Georama's modes (no build mode's scene, cursor or menus), else the town's own mode.
// CoopTownHidesPause: player 2's copy leaves out the pause's dim and sprite and keeps the textures moving.
// DC_COOP_MUTATE=townpause_shared: all three off, player 1's pause and Georama on both screens as before (twin).
void CoopTownStepAside();
// coop-menuanim: player 1's menu or pause with player 2 in the town, neither paused nor in her menu: both copies step the
// town as its walk does but for Toan (editloop, after CoopTownStepAside). The trace logs each change (runs or holds).
bool CoopTownWorldRunsAside();
void CoopTownWorldAsideTrace();
// coop-roommenu: the same in a room (edit_in steps its animations, water and people under his menu); the trace logs each
// change and, with DC_COOP_DEBUG, the room's first villager's motion clock while it runs.
bool CoopTownRoomRunsAside();
void CoopTownRoomMenuTrace();
// coop-roommenu: both decided by Note, once a tick after her room step (her body is put away by the room's first step):
// whether she is shown in the room (CoopTownP1MenuLive's test indoors) and whether the room runs under his menu.
void CoopTownRoomMenuNote();
bool CoopTownRoomPartnerShown();
// The room's mode as edit_in has it now (its own static GameMode, not ::GameMode), noted before his menu's step and the draw.
void CoopTownNoteRoomMode(int mode);
int  CoopTownRoomModeNow();
// coop-roommenu: the room's frame under his menu (whether the room was drawn, the projection it was drawn with and the room's
// own), traced every 30 frames with DC_COOP_DEBUG. Test hook DC_COOP_TEST_ROOM_MENU=K:H: together in a room, at her Kth step
// in it (both copies alike, Toan's room walking), his battle menu opens (Open, edit_in sets the room's goto-menu) and H ticks
// after it opened it closes (Close, read by the live menu's step).
void CoopTownRoomMenuDrawn(bool drawn, float projection, float room_projection);
bool CoopTownRoomMenuTestOpen();
bool CoopTownRoomMenuTestClose();
// Test hook (DC_COOP_TOWN_SPLIT=tick): on that walking tick (TownWarpForTest's count), player 2's copy alone presses
// the pause (EditLoop's Start), so the copies' town modes part and the checksum's town part must catch it.
bool CoopTownSplitForTest();
// The town's modes for the checksum: GameMode, player 1's pause, Georama and menu, player 2's own Georama, parts menu
// and menu (Checksum adds it on town ticks). Both copies step all of them alike.
std::uint32_t CoopTownModeSum();
int  CoopTownDrawnMode(int mode);
bool CoopTownHidesPause();
// Whether this copy leaves out the Georama's help bar (the other player's Georama).
bool CoopTownHidesBuildHelp();
// coop-uiflash test hook (DC_COOP_TEST_UNBUILD_APART=T): player 2's copy apart closes the game's Georama T ticks after it opened.
// DC_COOP_TEST_UNBUILD_APART_FROM_PEER=1: T ticks after it hears player 1's copy in her town instead (coop-localplay).
void CoopTownTestApartUnbuild();
// DC_COOP_DEBUG: each system message drawn while the help bar is left out.
void CoopTownTraceSystemMes();

// After the town's frame is drawn: the view and clip matrices (the next tick's culling reads them)
// and the random generator (the drawing draws from it) as the other copy left them. camera: the one the tick's view
// is from (the town's NowCamera when null; the interior passes its own).
namespace dc::game {
class CCamera;
} // namespace dc::game
void CoopTownViewEnd(CCamera *camera = nullptr);
// The camera the town's sky is drawn around (EditMainDraw's EdDrawSky): player 2's own on her copy while the frame is
// drawn from it, else camera (the town's NowCamera, his edit camera while he builds).
CCamera *CoopTownSkyCamera(CCamera *camera);

// coop-inbuild: player 2's character indoors (shown, inside, placed in this room): her position (3 floats), for the
// per-second checksum.
bool CoopTownIndoorPose(float *pos);
// coop-inbuild: the room walks this tick (Toan in none of its talk, event, menu, pause or door), for her own menu.
bool CoopTownIndoorWalks();
// coop-inbuild: the room is in Toan's menu (its setting up or running) this tick.
bool CoopTownIndoorP1Menu();
void CoopTownIndoorMenuLeft();
// coop-bldgsnap: the room's mode this tick (ED_IN_MODE_*, logs).
int CoopTownIndoorMode();

// coop-mayorhouse: her copy draws his talk's message window out of sight (true: drawn into a hidden target; End puts the
// screen back). Around edit_in.cpp's EditMes1.DrawMesWin.
void CoopTownHisTalkHiddenEnd(bool hidden);
void CoopTownClockStepBegin(); // coop-pt1108c: before the town's mode step (the clock put back after it when every player is in a menu)
bool CoopTownDayChangeHeld();   // coop-pt1109c: someone's talking, in co-op: the day change's event waits
void CoopTownDayChangeStarted(); // coop-pt1109c: said as its event starts

// coop-machotalk: the town's talks outdoors as a room's. His talk (the town's talk mode), linked and together: she walks on
// through it (editloop's talk mode steps her, CoopTownStep), and her copy draws its window out of sight (HiddenBegin, ended by
// CoopTownHisTalkHiddenEnd). TownTalkTick, each town tick before her talk's start: its lines, her talk ended by a split, the
// test hook (DC_COOP_TEST_TOWN_TALK).
// coop-pt1106-batch: a town event outdoors that is one player's own (Norune's fishing board): its reader (1, 2) while it runs, 0 for
// none or a shared one; its start (editloop, the event just set up); whether a player walks on through the other's; Toan's walk
// beside hers (his Cross off); its windows, sprites and fade out of sight on the other's copy (End: CoopTownHisTalkHiddenEnd).
void CoopTownTownEventStarts(int event_no);
bool CoopTownMapEventQueued(int event_no); // (a map event Toan's walk queued that her Cross may take; editloop)
bool CoopTownRoomEventQueued(int event_no); // (the same in a room: an event point Toan's walk queued; edit_in)
int  CoopTownOwnTownEvent();
bool CoopTownWalksThroughOwnEvent(int walker);
void CoopTownHisWalkInHerEventIn();
void CoopTownHisWalkInHerEventOut();
bool CoopTownOwnEventHiddenBegin(bool windows);
// coop-pt1106-batch: the frames a door opening together had its fade drawn out of sight on this copy, since last asked.
int  CoopTownDoorFadeHiddenTake();
void CoopTownTownTalkTick();

// Player 2's copy: the eye of the last frame drawn from her own camera and its height over her feet (DC_GROUND_TRACE).
bool CoopTownDrawnEye(float *eye, float *over);
// coop-localfix: a joiner's state set applied in a town: her model loaded again where she stands (its textures with it), before
// the port's texture registry is made again. DC_COOP_MUTATE=town_model_after_rebuild (twin): left to her next step, as before.
// (and before the registry: the host's partner textures out of the texture table)
void CoopTownSnapshotTexturesOut();
void CoopTownSnapshotApplied();

// coop-localfix: the town camera (NowCamera) eye and look now, for a desync's log (false out of town or with none).
bool CoopTownCameraNow(float *eye, float *look);
// coop-twinfix: her town camera's eye off its target distance, flat (-1: none); the same on both copies (tests).
float CoopTownCamEaseGap();
// coop-localfix: her town place, carried with a join's state set (an extra: taken on the host, put back on the joiner).
unsigned char *CoopTownSyncBytes(std::size_t *size);
void           CoopTownSyncTake();
// coop-houseexit (tests, DC_NETPLAY_TEST_TICK_DUMP): her town camera and walk state on one stderr line.
void           CoopTownCamTestLine(long long tick);
void           CoopTownSyncApply();
// coop-localfix2 (player 2's copy, a merge going by her state as player 1 arrives): her skin (the game's player walking as her)
// becomes the partner where she stands, her camera the one she sees through now, and the game's player Toan where player 1's
// copy has him (toan, yaw). False if she has no skin to become.
bool           CoopTownBecomePartner(const float *toan, float yaw);
// coop-stage4 4e part 2: her own event carried through her merge, the swap due at its end on every copy (coop_town.cpp).
unsigned char *CoopTownSwapDeferBytes(std::size_t *size);
bool           CoopTownSwapDeferred();
bool           CoopTownSceneWatchedTogether();
void           CoopTownSwapDefer(const float *toan, float yaw, int player);
void           CoopTownSwapDeferTick();
// coop-localfix2: player 2's copy waiting to join, walking as her in a town (its game started before player 1's arrived): a
// join that goes by her state, his Toan where he arrives (toan, yaw; null for the question alone).
bool           CoopTownPreJoinHere(float *toan, float *yaw);

// coop-localfix: the town tick's view from the game's camera, at its start (CoopGeoramaP1StepBegin's spot in the walk); and that view for
// the checksum (false indoors or under the twin).
void CoopTownTickView();
bool CoopTownCameraView(float (*view)[4]);

// coop-worlds: the town event player 2's partner stepped onto since the last call (a ground polygon's kind), else 0.
int CoopTownPartnerGroundEvent();

// coop-worlds: player 2's partner's position and rotation (both 4 floats), when she is in town.
bool CoopTownPartnerPose(float *pos, float *rot);

// coop-mergestand: where both stand on this copy now, for a resync's state (netplay.cpp TakeState, in the town mode): false
// indoors; partner_set when the town partner is shown (her 3 floats and yaw too), else Toan's 3 floats and yaw only.
void CoopTownSecondSpot(const float *spot, float yaw, const float *other, float *out); // coop-townstand
bool CoopTownStandsForResync(float *toan_pos, float *toan_yaw, bool *partner_set, float *partner_pos, float *partner_yaw);

// coop-worlds: a test puts player 2's partner at pos (her collision lands it on the ground below), facing pos[3].
void CoopTownPlacePartnerForTest(const float *pos);

// coop-localfix: player 2's door while player 1's town menu or pause holds the town's mode (editloop's PartnerDoor, after
// CoopTownStepAside): whether her door is looked for now, and, on finding one, the split and her copy out of his menu or
// pause first. CoopTownHerDoorTakesOver returns whether this copy runs her door's event.
bool CoopTownHerDoorAside();
bool CoopTownHerDoorTakesOver(bool cave);
// coop-doorprompt: the player (1 or 2) whose cave key opened the cave door this tick, else 0 (coop_town.cpp). coop-doorfix:
// walked: that player walked in beside the game (together), now to split and jump; else the door's event is to run.
int CoopTownCaveKeyUsed(bool *walked);
// coop-doorfix: the cave door's acts on a player (retail's refusal of a wrong item; the walk in by the key or the opened
// door's Cross), beside the game on both copies: whether one runs, whether it holds the game's pad (port 0), the refusal's
// and the walk's starts (the walk false when it does not apply: apart), the user's fade (draw), and a Cross at the shut door
// as that player's own note.
bool CoopTownCaveActs(int player);
bool CoopTownCaveHoldsGamePad();
void CoopTownCaveRefusal(int player);
bool CoopTownCaveEntry(int player);
void CoopTownCaveDraw();
bool CoopTownCaveDoorShutNote(int player);
bool CoopTownCaveCross(int player, bool open);
// coop-menucross: her pad is the world's (no menu, pause, Georama or wheel of hers has it, nor let go of it on this tick);
// her Cross at the shut cave door shown as her own note (no game event), true when it took the Cross.
bool CoopTownHerPadFree();
bool CoopTownHerCaveDoorShut();
int  CoopTownPartnerMember(); // coop-uinotice: the member pad 2 plays in town
