#pragma once

#include "coop/game_ns.hpp"

#include <cstdint>

// coop-worlds: separate worlds for linked copies (game.netplay 1 or 2). One shared game, two copies:
// while both players are on the same map (the same town, or the same dungeon floor) the copies run in
// lockstep as today (TOGETHER). When player 1 takes one of the game's exits the pair splits on that
// tick, which both copies reach alike: player 1's copy leaves alone, player 2's copy stays where it is
// with player 2 as the game's own player (its member, where its partner stood) and no partner. The
// copies are then APART: each runs its own world, the link stays connected (presence messages over the
// link's socket, never lockstep), and the HUD says where the other player is. Apart copies never link
// again on their own; a merge does, through CoopWorldsMayLink: for now when both are in the same town
// and each has heard that the other knows it (presence carries what each last heard) and has said it is
// ready (so neither starts the link alone and waits it out), and the link's own
// start brings them together from the host's state. A merge whose link drops at once is apart again.
//
// On for every linked pair (the Linked and Online launchers); DC_COOP_WORLDS=0 turns it off. Single-process
// Co-op is never linked, so it never splits.

// Each tick, from the link's tick (NetplayTick, before it listens or ticks): whether the link is up and
// its tick. Read only: it sends and logs, and changes nothing the game simulates.
void CoopWorldsTick(bool up, std::int64_t tick);

// The link's Receive hands every datagram from the other player here first: true when it was a worlds
// message (magic "WWCD"), which this takes; false for everything else.
bool CoopWorldsReceive(std::uint32_t magic, const void *data, int size);

// Whether a Hello from a new session may start a link: true unless the copies are apart (and not
// merging). Logs each link it holds back.
bool CoopWorldsMayLink();

// A deterministic exit point (a map exit the game is about to take, on the same tick in both linked
// copies): whether this copy takes it. While linked with worlds on, the pair splits here: player 1's copy
// takes it, player 2's copy stays (false) and becomes player 2's own world. True whenever worlds is off
// or the copies are not linked. next_map: where the exit goes (NextMapNo's numbering).
bool CoopWorldsTakeExit(int next_map);

// Player 2's copy staying on a dungeon floor (after CoopWorldsTakeExit said false): the game's player
// becomes player 2's member where its partner stood. The exit's own fade is the caller's to undo.
void CoopWorldsStayOnFloor();

// Player 2's copy staying in town (after CoopWorldsTakeExit said false): Toan, the town's player, stands
// where player 2 stood.
void CoopWorldsStayInTown();

// coop-p2entry: player 1's copy staying in town after player 2's partner took a town event's way out: whether its Toan
// goes back where he stood as the event began (the town's loop keeps that place).
bool CoopWorldsUndoTownEvent();

// The town's test exit (each tick of town walking, both copies): whether player 1 goes through the town's
// dungeon gate now, as its script does (goto_dungeon). DC_WORLDS_EXIT_AT=N: at link tick N (linked);
// DC_WORLDS_APART_EXIT_AFTER=K: an apart copy, after K ticks of play.
bool CoopWorldsTestTownExit();

// The story guard, at each event the player walks or talks into (the town's start of an event MainMode or
// TalkMode queued; an interior's RunEvent), both copies: true when the event is held
// because it is story and Toan is not here: player 2's copy while apart, an event whose script can set a game
// flag (_SSET_GAME_FLAG, read from its code). The HUD then says "Toan needs to be here". program: the loaded
// event script (EdEventData).
bool CoopWorldsHoldStoryEvent(const void *program, int event_no);
// coop-p2collide: whether a room's event is a scripted pickup, no story (the mayor's closet; the guard's own rule): player 2's
// Cross starts one together too (CoopTownHerRoomEvent).
bool CoopWorldsPickupEvent(const void *program, int event_no);
// coop-roomevent: a room event a player may start as their own (a pickup, or one that writes no story and goes nowhere).
bool CoopWorldsPartnerRoomEvent(const void *program, int event_no);

// coop-p2scene: the story guard at a room's arrival event (EditInInit's start event, which starts by itself): true on player
// 2's copy apart when that event can still set a game flag this copy has not set (the mayor's first visit), so it waits
// for player 1. An arrival whose story flags are all set, or that writes none, runs as before. coop-heldscene: true on
// player 1's copy too when he comes in where she already is (it plays after the merge, CoopWorldsArrivalAgain).
bool CoopWorldsHoldArrivalEvent(const void *program, int event_no);

// coop-mayorhouse: her copy's held arrival runs as if its story were seen: the script's read of game flag flag (value as
// the save has it) and whether its flag writes are dropped (editloop3.cpp's _SGET_GAME_FLAG and setters).
int  CoopWorldsScriptGameFlag(int flag, int value);
bool CoopWorldsScriptWritesHeld();

// coop-mayorhouse: link ticks since the last merge, linked and together; else -1.
std::int64_t CoopWorldsTicksSinceMerge();

// coop-heldscene: each room step, before its events start (room_mode: the room's GameMode): the room's arrival event to
// start now, else 0. After a merge by her state inside a room, on the first walk step, both copies alike: its arrival if its
// story is still not seen (held on her copy, coop-p2scene, and on his as he came in after her), so player 1 gets it; on his
// copy alone if she left the room before the merge.
int CoopWorldsArrivalAgain(const void *program, int event_no, int room_mode);

// The town's test event (each tick of town walking): an event number for RunEvent once, else 0.
// DC_WORLDS_TEST_EVENT=N (or "story": the first program in the town's script, past 128-150, that can write story) on an
// apart copy, DC_WORLDS_TEST_EVENT_AFTER=K ticks of town play after the split (default 300).
int CoopWorldsTestTownEvent(const void *program);

// DC_WORLDS_MUTATE names this feature (the worlds twins' switch), for code outside coop_worlds.
bool CoopWorldsMutated(const char *feature);

// A town event's own jump into a dungeon (the script's _MAP_JUMP, ED_EVENT_RETURN_MAP_JUMP: the town's other way
// out besides goto_dungeon), both copies, after the event set it: whether this copy takes it (CoopWorldsTakeExit's
// rule: player 1's copy leaves, player 2's stays). next_map: the jump's map (200 on for a dungeon).
bool CoopWorldsTakeScriptExit(int next_map);

// Player 2's partner landed on a town polygon whose event is event_no (the ground's kind; program: the town's script):
// whether she starts it (linked and together, an event that leaves by its own _MAP_JUMP and goes into no building).
bool CoopWorldsPartnerMayStart(const void *program, int event_no);

// The town event just started is player N's (2: her partner's); its exit's split sends that player's copy and keeps the
// other in town.
void CoopWorldsTownEventBy(int player);
// coop-dungeonp2: the dungeon's way out (the menu's Escape) chosen by player N, both copies on the same tick: its split
// sends that player's copy and keeps the other on the floor (Jep: Xiao's "escape this dungeon" took Toan out).
void CoopWorldsExitBy(int player);

// While the town event player 2 started runs (coop-p2collide: or a room's): the town's pad (port 0) is hers, on both copies.
bool CoopWorldsP2Drives();

// A door into building name (GotoInterior, both copies): whether this copy goes in (the opener's does; the other stays
// outside and the copies are apart until it comes back out).
bool CoopWorldsTakeInterior(const char *name);
// coop-inbuild: a door out of the building the pair is in together, taken by player N (both copies, the same tick):
// whether this copy goes out (the taker's does; the other stays inside, apart, its player where player 2 stood).
bool CoopWorldsLeaveBuildingBy(int player);
// coop-pt1106-batch: together in a room, the leaver's door opens on both copies first, the split as it ends: Start at the door's
// press (true: the window began, both copies play the door; false: as before, CoopWorldsLeaveBuildingBy at once); By: whose
// door opens together now (0 none; linked and together only); End: the window over (the caller splits).
bool CoopWorldsDoorWindowStart(int player);
int  CoopWorldsDoorWindowBy();
void CoopWorldsDoorWindowEnd();

// DC_WORLDS_MUTATE=no_partner_door (the door test's twin): player 2's Cross at a door opens nothing.
bool CoopWorldsMutatedDoor();
// coop-menucross: linked and together (CoopWorldsPartnerMayStart's own test, without her pad's).
bool CoopWorldsLinkedTogether();

// Test hook (DC_WORLDS_TEST_LEAVE_BUILDING=K), each tick inside a building: true when an apart copy leaves it now.
bool CoopWorldsTestLeaveBuilding();

// The first town walk after a merge inside a building: the building's number (CoopWorldsInteriorId of its name) to go
// back into, both copies (once), else 0.
int CoopWorldsReenterBuilding();

// coop-housemerge: a building's interior set up (EditInInit's end, before its first tick): the host alone inside takes
// its checkpoint there; a joiner catching up to that checkpoint takes the host's state again there (NetplayInteriorStarted).
void CoopWorldsInteriorStarted();

// A building's number (its name, as a zone keeps it).
int CoopWorldsInteriorId(const char *name);

// coop-joinfix: a resync taken inside building (its number and name): the town's load, then both back in on their first walk.
void CoopWorldsResyncReenter(int building, const char *name);

// Test hook (DC_WORLDS_TEST_PARTNER_WARP=N:X:Z): true (and pos) on the link tick player 2's partner is put at (X, Z).
bool CoopWorldsTestPartnerWarp(float *pos);

// The gate test's walk-on (each tick of town walking, both copies): DC_WORLDS_TEST_GATE_WARP=N:X:Z, at link tick N
// (linked and together) Toan is put at (X, Z), onto the town's entrance, and the game starts its event as when he walks
// there (the cave is marked entered first, so its world map offers it); DC_WORLDS_TEST_GATE_APART=K, the copy
// left in town again every K ticks of its own walking. True (and pos) on those ticks.
bool CoopWorldsTestGateWarp(float *pos);

// Test hook (DC_WORLDS_TEST_HOLD_EXIT=N): the copy that left a town by an exit stays in it its first N ticks after the
// split (the town's loop goes on, as a slow fade or load would keep it), so a merge can come before it is out.
bool CoopWorldsTestHoldExit();

// The gate test's facing for Toan with its warp (DC_WORLDS_TEST_GATE_WARP's fifth field), else NAN.
float CoopWorldsTestGateYaw();

// Each frame, after it is drawn (the main loop): the tests' screen checks after a split (DC_WORLDS_TEST_SHOTS).
void CoopWorldsAfterFrame();

// Each mode's end, before its map jump is followed (from_mode: the mode that ran): a town's jump into a dungeon (200 on) while linked
// and together took a way out that does not split, and is logged ("worlds: UNSPLIT town exit"), for the tests.
void CoopWorldsCheckTownLeave(int from_mode, int next_map);


// The door vote, at each dungeon script start (GAME_TASK_SCRIPT_START, each tick while it runs), linked and
// together: when the script is the floor's exit gate (its event named "out"), the player who did not start it is
// asked "Leave too?" (Cross yes / Circle no, 10 s). Yes: both go through, as before. No: the one who started it
// is asked "Player N declines. Leave anyway?": yes splits the pair (that player's copy goes through, the other's
// stays on the floor, apart), no keeps both on the floor. True while the gate's script must not start. The
// questions show on the HUD's link line of the copy asked (CoopWorldsStatus).
// coop-floorgate: a next floor whose opening scene is unseen, or whose boss is alive, asks "Player N wants to go to <floor>.
// Join?" instead, with no leave anyway: a no keeps both on the floor and tells the asker. Apart, such a gate does not start
// (CoopWorldsHoldFloorChange) and the other copy is called.
bool CoopWorldsGateVote(int script_no);
// coop-floorgate: whether this copy's player may be asked a question now (playing a floor or walking a town; no scene, event,
// talk, minigame or menu). A question that would come to a player not free is answered "busy" instead (never shown).
bool CoopWorldsPlayerFree();
// coop-floorgate: game.coop_story_gather as it is in effect (a joiner takes the host's).
bool CoopWorldsStoryGather();

// coop-p2prompt: player 2's exit gate asked from her own prompt, beside the running game (coop_worlds.cpp): whether a gate
// may be asked now, the ask (asker 0 player 1, 1 player 2; answer: the escape window's, 1 the next floor, 2 back to town), whether such a question is waiting, and whether it waits on player.
namespace dc::game {
class CDungeonEventData;
} // namespace dc::game
bool CoopWorldsGateAskable();
bool CoopWorldsGateAskBeside(int asker, CDungeonEventData *gate, int answer);
bool CoopWorldsGateAsking();
bool CoopWorldsVoteAsks(int player);
// coop-floorgate: the gathered gate's question while it waits on a player (asked and asker 1 or 2; the dungeon and the floor,
// 0 up; gather 1 its opening scene unseen, 2 its boss alive; cursor 0 go together, 1 not yet), drawn in the game's window
// on the asked player's copy (coop::DrawGatherQuestion).
bool CoopWorldsGatherQuestion(int *asked, int *asker, int *map, int *to, int *gather, int *cursor, int *stage, int *where);
// coop-gatevote: the asker's box on its copy: who it waits on (state 0 waiting, 2 not coming), where to, LEAVE NOW allowed, its hand.
bool CoopWorldsGateWaiting(int *waiting_on, int *state, int *where, int *to, bool *can_leave, int *cursor);
// coop-floorgate: apart, the other player's call shown on this copy (from 1 or 2; the dungeon, the floor called to and the
// caller's floor, 0 up; cursor 0 come, 1 not now), drawn in the game's window (coop::DrawGatherQuestion).
bool CoopWorldsCallQuestion(int *from, int *map, int *to, int *floor, int *cursor);
// coop-floorgate: "Divine Beast Cave floor 2" (to 0 up).
const char *CoopWorldsFloorName(int map, int to);

// Joining on the next floor, at each dungeon script start (GAME_TASK_SCRIPT_START, each tick while held): true
// while an apart copy's floor exit gate (the floor's event named "out", script_no its script) waits for the
// other player, who is at this dungeon's entrance; the merge's resync then takes both to the entrance's floor
// menu. Up to 20 s, then that floor's gate goes on alone.
bool CoopWorldsHoldFloorChange(int script_no);

// Whether the copies are apart: the partner is then on neither copy (not stepped, drawn or targeted).
bool CoopWorldsApart();
bool CoopWorldsPeerHeardHere();
long long CoopWorldsHerLeaveMs(); // coop-dunentry: ms since player 2 left this (player 1's, staying) copy's map, else -1
void CoopWorldsLocalFlags(bool on); // coop-floorscene2: flags set while on stay this copy's (no progress sent)

// coop-natsplit (Jep, 2026-10-28: Spacewar only for the handoff): the copies are apart by a split (not a dropped link) and
// the other copy still talks to this one (its last word, or the split, under 10 s ago): the direct link will come back.
bool CoopWorldsSplitPeerAlive();

// coop-localfix2 ("whoever is already there wins", Jep 2026-10-26: "if P2 is in the area before P1, wouldnt P1 have to catch
// up?"): this copy was in its town or building first (coop-houseenter: whoever moved last arrives), the other copy is the
// one arriving: the merge's state goes from this copy and the arriving one catches up behind its own door's fade. False for a merge anywhere else, and with
// DC_NETPLAY_MUTATE=host_always_sends (twin): the host's state, as before.
bool CoopWorldsSendsMerge();

// coop-houseenter: apart, the other copy is heard in this copy's zone (it has come where this one is: a merge to come).
bool CoopWorldsPeerHere();

// coop-localfix2: where the other copy's player stands now (its presence), in this town: false if not heard there.
bool CoopWorldsPeerStand(float *pos, float *yaw);

// coop-georamaload (Jep, 2026-10-27: Xiao in the Georama before Toan's first load-in was thrown out of it, and his copy
// never finished loading): player 2's copy waiting to join (no link yet, not apart) builds in the game's Georama in a town's
// outside. Her Hello says so (builds) and neither copy starts the first link until she closes it; his copy plays on alone.
bool CoopWorldsPreJoinBuilds();

// coop-georamaload: this copy's player walking in a town's outside (Toan, on player 1's copy): where, for its Hello.
bool CoopWorldsOwnStand(float *pos, float *yaw, std::int32_t *map);

// coop-localfix2: this copy came into a town (coop-houseenter: or a building) within the last within_ms where the other copy
// was first: the merge to come goes by the other's state, this copy catching up.
bool CoopWorldsArrivedWhereStayer(int within_ms);

// coop-scenejoin: a state set may be taken while a room's scene (its event) runs, so a merge goes into the scene
// (DC_WORLDS_MUTATE=scene_merge_off: no, as before).
bool CoopWorldsSceneTakeOk();
// coop-exitscene: and while a town's event runs outdoors on player 1's copy (DC_WORLDS_MUTATE=town_scene_merge_off: no).
bool CoopWorldsTownSceneTakeOk();
void CoopWorldsBagsHers();        // coop-stage4 4e part 2: her bag in the party's place (CoopWorldsBagsBack undoes it)
bool CoopWorldsHerEventCarries(); // coop-stage4 4e part 2: her copy apart, her own talk or event running: it goes on through the merge
bool CoopWorldsTalkTakeOk(); // coop-stage4 4e part 1: his talk is a take tick
// coop-exitscene: a town event about to start whose only way out is into a dungeon splits a pair together first; whether
// this copy runs it (DC_WORLDS_MUTATE=dungeon_event_together: both run it till its jump, as before).
bool CoopWorldsIntoDungeonOnly(const void *program, int event_no); // coop-pt1109c
bool CoopWorldsStayedHere(); // coop-pt1109f: apart, and this copy is the one that stayed when the other left
bool CoopWorldsDungeonEventAlone(const void *program, int event_no);
// coop-doorgate: Norune's cave door while B1's opening scene is unseen, used by player `asker` (1 or 2) while the other player
// is linked but not beside it (game.coop_story_gather): it stays shut, the asker gets a note, the other a call. Whether it
// stays shut (the event's Cross and Square: CoopWorldsDungeonEventAlone; the key picker's Square: coop_town).
bool CoopWorldsCaveDoorRefused(int asker);
// coop-doorgate: the door into room `name` (a building's, "i01h06") used by player `asker` stays shut: its arrival's story is
// unseen and the other player is linked but not beside it (the note and call as above). GotoInterior asks it itself
// (CoopWorldsTakeInterior); her door under his menu asks first (coop_town).
bool CoopWorldsDoorRefused(const char *name, int asker);
// coop-doorgate: the Georama menu's Event pick of town event `event_no` (200 + the pick) while its story is unseen and the other
// player is linked but elsewhere (apart): it does not start, the asker gets a note, the other the call. Together it plays for
// both as before. Whether it stays shut (port/src/editloop.cpp's EDIT_MENU_MODE_EVENT).
bool CoopWorldsGeoramaEventRefused(int event_no);
// coop-doorgate: the shut door's call as a question in the game's window on the asked player's screen ("Come" / "Not now",
// the hand at row cursor; key tells one laid-out text from another), drawn by coop_useprompt; its keys from the town's
// game pad (coop_menu's MenuFilterPort, port 0, both copies alike: Up, Down, Cross and Circle held back from the game while
// it asks this copy's own player); and whether it asks player `player` (her door's Cross waits meanwhile).
bool CoopWorldsDoorQuestion(int *asked, char *text, int size, int *cursor, int *key);
// coop-gatepopup: that question's text for asker at door table row `row` (0 to CoopWorldsDoorRows() - 1), the cave door, or a
// Georama event's row (-2 - the town); the popup tour (DC_COOP_TEST_POPUP_TOUR) lays out every one.
void CoopWorldsDoorQuestionText(int asker, int row, bool cave, char *text, int size);
int  CoopWorldsDoorRows();
void CoopWorldsDoorQuestionPad(int *buttons);
// coop-gatepopup: while a gate's question waits, the asker and the asked stand as in their own pause (port 0 this copy's game
// player, together player 1; port 1 player 2's partner, together): coop_menu, coop_town and coop_pause hold their pads, and a
// floor's world holds when every player there is held.
bool CoopWorldsQuestionHolds(int port);
// coop-gatepopup2: apart at a dungeon's entrance floor menu, the pick of a floor that needs both players is held back and the
// other player called (port 0's buttons, coop_menu's MenuFilterPort); their yes picks it.
void CoopWorldsEntrancePad(int *buttons);
// coop-gateflow: player's Cross at a floor's exit gate (her own prompt's), with game.coop_gate_picker: to the dungeon's floor
// menu, no question (together the pair splits there and the other is asked to go along). False: the gate as before.
bool CoopWorldsGatePicker(int player, CDungeonEventData *gate);
// coop-gatepopup3: port 0's buttons as the game reads them (MenuFilterPort, before any hold), and what an apart call's keys read.
void          CoopWorldsGamePad0(int *buttons);
std::uint16_t CoopWorldsCallPadButtons();
// coop-gatepopup: this copy's player asked a gate's question and waits on player `asked`'s answer (coop::DrawWaitingBox draws its
// box: "Waiting for Player N", the controller's Circle: Cancel; Circle withdraws it).
bool CoopWorldsWaitingFor(int *asked);
bool CoopWorldsDoorAsks(int player);

// coop-scenejoin: a room's event about to start is a story scene (its arrival, its story not seen): both screens show it from
// the game's camera, her held and hidden (DC_WORLDS_MUTATE=scene_view_own, in coop_town.cpp's SceneHolds: never held, her own
// view as before).
bool CoopWorldsSceneEvent(const void *program, int event_no);
// coop-saveload: player 1 loaded a save from his menu while apart: her copy hears it (a notice); the merge takes his.
void CoopWorldsLoaded(int file_no);
// coop-loadpull: player 1 loaded a save apart and the copies are not merged since: the merge goes by his state, wherever she is.
bool CoopWorldsLoadPull();

// coop-itemsend: whether player 2's copy apart plays from her bag (the game's pack holds hers, g_bag his).
bool CoopWorldsBagsSwapped();
// coop-p2toan: the bag test's dump now (DC_WORLDS_TEST_BAGDUMP; the lockstep's first tick: whose bags the join left).
void CoopWorldsBagDump(const char *label);

// coop-bagapart: test hook (DC_WORLDS_TEST_APART_PRESS_P2): the buttons her copy apart holds this tick, for the game's pad.
std::uint16_t CoopWorldsTestPadButtons();

// coop-floorsnap (stage 4): the merge ready or under way is a floor join: both copies apart on one dungeon floor (player 2
// picked the floor player 1 is on, or took the gate onto it). It goes by the host's snapshot (netplay's catch-up), never
// the reload to the entrance; a plain replay could not carry the floor's layout, which is rolled before the checkpoint.
bool CoopWorldsFloorJoin();

// coop-floorjoin2: apart, the floor (0 up) the other copy was last heard on in dungeon `map`, -1 when it is not there (or
// at its entrance, -1 too): a floor this copy starts with the other on it merges by the host's snapshot at once.
int CoopWorldsPeerFloorIn(int map);

// coop-dungeonjoin: in a dungeon, the floor (0 up) this copy is on, -1 at the entrance's floor menu, -2 on its way to a
// floor (picked, not started yet); -1000 outside a dungeon.
int CoopWorldsHereFloor();

// coop-dungeonjoin: this copy set out for a floor within the last within_ms with the other already on a floor of this
// dungeon, and is on the other's floor, apart: the merge to come has it catch up (asked each frame).
bool CoopWorldsArrivingOnFloor(int within_ms);

// coop-cutscenejoin: at a floor's "go dungeon" (each tick while held): true while this copy, arriving apart, waits for the
// other copy's opening scene of that floor to end (it was there first): the floor then starts and the pair joins on it.
bool CoopWorldsHoldFloorStart();

// coop-cutscenejoin: whether this copy's floor start is held now (CoopWorldsHoldFloorStart; read only).
bool CoopWorldsFloorStartHeld();

// coop-cutsceneview (Jep's 2026-10-27 playtest: P2 black while P1 plays the floor's opening scene; he wants Toan's view): while
// this copy's floor start is held (CoopWorldsFloorStartHeld) and the other copy's view of its scene came in the last half
// second, that view's eye, look point and roll (world places): this copy draws its own build of the floor (the same one,
// from the other's random state) from it instead of black. False otherwise. DC_WORLDS_MUTATE=scene_black (twin): never.
bool CoopWorldsSceneView(float *eye, float *ref, float *roll);

// coop-cutsceneview (tests): the other's count of the view this copy shows now (CoopWorldsSceneView), 0 for none.
unsigned CoopWorldsSceneViewSeq();

// coop-cutsceneview: whether this copy, showing the other's scene (CoopWorldsSceneView), draws its player as the other's
// (the game's Toan the dungeon loaded, posed where the other copy draws its Toan, when the other plays Toan). Read only.
bool CoopWorldsSceneShowsOther();

// coop-tutimages (Jep's 2026-10-27 playtest: no tutorial pictures on P2's screen in the B1 scene): the scene's 2D pictures
// (event sprites in texture blocks 45-49) on the copy showing the other's scene (coop_scenepics.cpp). The copy playing it
// says what its blocks were made of and which sprites it draws, with each SceneView (CoopWorldsScenePicturesSend); the
// showing copy loads the same pack entries from its own game data (CoopWorldsScenePicturesTick, each tick) and draws the
// sprites over its view (CoopWorldsSceneSpritesDraw, the dungeon's 2D pass). DC_WORLDS_MUTATE=scene_no_pictures (twin): none.
void     CoopWorldsSceneChrFile(int index, const char *path);                       // _LOAD_CHR_FILE's packs
void     CoopWorldsSceneTexture(int block, int file, const char *entry, int order); // a block's entries, in order
void     CoopWorldsScenePicturesSend(std::uint32_t magic, int player, std::uint32_t seq, int map, int floor);
unsigned CoopWorldsScenePicturesSent(); // (tests) the sprites in the last one sent
bool     CoopWorldsScenePicturesReceive(const void *data, int size, int other);
void     CoopWorldsScenePicturesTick(bool apart, int map, int floor);
void     CoopWorldsSceneSpritesDraw();

// coop-b1lesson: a join in the other's floor scene (CoopWorldsSceneJoining) puts the other's state in this copy, with its
// actors' mesh records naming models the other read into the dungeon's read buffer, which the state set leaves out. The
// copy playing the scene names the packs it read (with each SceneView); the joining copy reads them from its own game data
// while it waits (CoopWorldsScenePacksTick, each tick), and the port's mesh rebuild finds a model there by its header's
// print (CoopWorldsSceneModel, port/src/visualvu1.cpp's Draw3DEnsureVisual), nullptr if none.
constexpr int kCoopWorldsScenePacksBytes = 508; // its message's size (told apart from the other worlds messages by it)
void          CoopWorldsScenePacksTick(bool scene, bool joining);
unsigned int *CoopWorldsSceneModel(std::uint32_t print);

// coop-cutscenejoin: the floor (0 up) of this dungeon whose opening scene the other copy plays now (on its way onto it,
// built, not started), -1 none.
int CoopWorldsPeerOpeningFloor();

// coop-cutscenejoin: this copy, apart, is in its own arrival on the floor the other plays (picked it, built it, the floor's
// entrance animation under way): it counts as on that floor, and the other's state lands over the arrival.
bool CoopWorldsPickJoining();

// coop-b1lesson (Jep's 2026-10-27 playtest: P2 on B1 during P1's elder lesson saw its pictures, but not the elder, his
// speech bubbles or herself): this copy, apart, joins the other's floor during that floor's opening scene: 1 as the one
// playing the scene (it sends its state, taken mid-scene), 2 as the one held for it (CoopWorldsHoldFloorStart; it takes
// that state and plays the rest of the scene in lockstep), 0 neither. Both count as on that floor (Here) until the merge.
int CoopWorldsSceneJoining();

// coop-b2join: the floor's arrival (script 100) plays a scene of its own, not only the walk-in: B2's, the cat (an event
// actor loaded). The scene join takes it as a scene, and the partner's arrival camera and walk (FloorIntro) stand aside.
bool CoopWorldsArrivalPlaysScene();

// coop-b1lesson: whether the partner stands, drawn, through the floor's scene under way (together, the floor not started
// yet: its opening scene, which both copies play). Read only.
bool CoopWorldsPartnerStandsInScene();

// coop-cutscenejoin: this copy, apart, set out for a floor the other was on or opening (read only; tests).
bool CoopWorldsArrivingApart();

// coop-cutscenejoin: player 1's copy, as a join by its state begins on a floor player 2 arrived on: she is to stand where
// her copy last said she stands (her arrival's end), not beside him (coop::Partner::arrive_floor).
void CoopWorldsArriverStand();

// coop-dungeonjoin: player 2's copy, as a floor merge by its state begins (it was on the floor first): her member, the game's
// player, becomes the partner where she stands (her camera kept), player 1's member the player where his copy last said he
// stands, so the state set it sends is the pair's, as one on the host's makes it. False if it could not.
bool CoopWorldsFloorBecomePartner();

// coop-dlgname: whether the game's dialogue on this copy is player 2's (her copy apart, or the town event she started
// while together, on both copies alike). Read only.
bool CoopWorldsPartnerSpeaks();

// coop-floorsnap: the next CoopWorldsSimTick logs the floor layout line again (the snapshot put another copy's floor here).
void CoopWorldsFloorLayoutAgain();

// coop-floorsnap: a floor started on this copy ("go dungeon", from netplay's NetplayFloorStarted): from here the zone
// names the floor. Between a gate's floor number and the floor's start the copy is "on the way" (floor -2), so no merge
// begins on a floor that is not built yet (w-floor-pick run 1: the host's floor 2 started with the link already up).
void CoopWorldsFloorBuilding(); // coop-twinfix: her Toan hidden from a floor's build, apart
void CoopWorldsFloorStarted();
// coop-fx51c (tests): DC_WORLDS_TEST_SLOT_AT_MERGE_WHEN=catchup's change, as this copy's catch-up begins (netplay.cpp).
void CoopWorldsTestSlotOnCatch();
void CoopWorldsTestSlotOnLastBlob(); // coop-catchuprace (tests): DC_WORLDS_TEST_SLOT_AT_MERGE_WHEN=lastblob
// coop-fx51c: a state file (DC_STATE_LOAD_AT's fixture) put back on a floor: the floor counts as started and played (netplay.cpp's
// StateFileApply).
void CoopWorldsStateFileLoaded();
// coop-warmreset (WS7): a warm pair's job start (coop_core's CoopResetForJob): no door vote left, the said-once lines unsaid;
// and the vote as a job finds it, for the job's leftovers line.
void CoopWorldsResetForJob();
// coop-quittitle: this copy's session ended by Quit Game (netplay.cpp NetplayTakeQuitToTitle): the pair's state (apart, the
// merge, the other copy's presence, the vote) as a booted copy has it.
void CoopWorldsSessionEnded();
void CoopWorldsJobLeftovers(char *out, std::size_t size);

// A resync's state taken (netplay.cpp's TakeState, on both copies): a floor change held for a merge ends. coop-floorscene: own,
// a checkpoint's self-take (this copy's own state): not the other's state in.
void CoopWorldsStateTaken(bool own = false);

// The HUD's link line while apart ("Apart: player 2 is in Norune"), else empty.
const char *CoopWorldsStatus();

// Each tick of dungeon play on both copies (CoopStep): the test hook DC_WORLDS_EXIT_AT=N (both copies)
// starts player 1's way out of the dungeon (its exit fade) at link tick N, as an exit would; an apart
// copy with DC_WORLDS_APART_EXIT_AFTER=K takes its own way out after K ticks of play.
void CoopWorldsSimTick();

// Player 2's progress from apart (what, a, b, c, d) applied to this copy's save: the host as it takes it, and a
// catch-up joiner as its replay reaches the tick the host took it on (NetplayCatchupDelta).
void CoopWorldsApplyProgress(const std::int32_t delta[5]);
// coop-dungeonp2: player 2's copy apart with separate bags plays from her bag (the game's pack holds it there);
// a link's state, sent or taken, carries each bag in its own place, so both calls put them back first.
void CoopWorldsBagsBack();
// After a save: player 2's copy apart plays from her bag again (a save writes each bag in its own place: player 1's
// in the game's save, hers beside it; CoopSaveBagsHome below).
void CoopWorldsBagsFollow();
void CoopWorldsBagsSaveBack(); // CoopWorldsBagsBack, unless DC_WORLDS_MUTATE=no_save_home (the save test's twin)
// coop-keyring: apart, the other player is on a dungeon floor (or on the way down to one): its map and floor (-2 on
// the way). False together, unlinked, or with the other player in a town or at a dungeon's floor menu.
bool CoopWorldsPartnerOnFloor(int *map, int *floor);
// coop-gateopen: this copy opened a dungeon floor's gate (coop::GateOpenTick); apart, the other copy is told (kWhatGate).
void CoopWorldsGateOpened(int dungeon, int floor);
// coop-joinatla: the game's first entry into a dungeon lays out its Atla (BtAtraListMake, from the random state): the
// state it was laid out from is kept, and goes with the floor's progress, so the other copy lays out the same.
void CoopWorldsAtlaListMake(int dungeon);
struct CoopSaveBagsHome {
    CoopSaveBagsHome() { CoopWorldsBagsSaveBack(); }
    ~CoopSaveBagsHome() { CoopWorldsBagsFollow(); }
};

// coop-localfix: player 2's copy apart in a dungeon, before her member plays there (the entrance's scene, a floor's
// opening): its player, the game's Toan, is not drawn (port/src/dun/gameloop.cpp's draw). Logs what it draws.
bool CoopWorldsHideDungeonPlayer();
// coop-joinvisuals: the member player 2's copy draws in the hidden player's place this floor frame (her entrance), -1 none.
int CoopWorldsEntryStandIn();
// coop-sceneowner: her floor merge in her own floor's opening scene holds its change of players until the scene's end
// (coop_worlds.cpp, CoopWorldsFloorBecomePartner); in the state set. CoopWorldsSceneOwner: 2 while it holds (linked, on
// the floor): the game's pad is hers (NetplayRoute), else 0.
namespace coop {
struct SceneHandover {
    int   due;    // 1 while her scene plays on
    int   her;    // her member: the partner at its end
    int   member; // player 1's: the game's player at its end
    float toan[3];
    float yaw;
};
extern SceneHandover g_scene_handover;
} // namespace coop
int CoopWorldsSceneOwner();

// coop-localfix: what player 2's copy apart draws as its own player (chara 0-5, -1 nobody), logged at each change
// ("identity: player 2's copy draws its player as Toan (town, ...)").
void CoopWorldsIdentity(int chara, const char *where);
// coop-localrace: her copy has said whom it draws and where she stands before any link (the -HerFirst test hook).
bool CoopWorldsPrelinkSaid();

// coop-localfix: player 2's cave door taken while player 1's town menu or pause is open (coop_town's
// CoopTownHerDoorTakesOver), both copies on the same tick: the pair splits now, she leaving for the cave, before her
// event runs on her copy alone.
void CoopWorldsSplitForHerEvent();
// coop-doorprompt: the same for either player's cave key at the cave door: the pair splits now, player leaving for the cave,
// so the door's event (its fade and jump) runs on that player's copy alone. True on the copy that goes (and when not
// linked together); the other copy stays in town playing (CoopWorldsStayInTown).
bool CoopWorldsSplitForEvent(int player);

// coop-localfix: a merge's host state put in this copy while still apart (netplay's snapshot apply; a resync's
// CoopWorldsStateTaken sets it too): from there its game's player is player 1's, hers the partner, till the merge ends.
void CoopWorldsMergeStateIn();
bool CoopWorldsMergeStateInHere();
// coop-localfix (Jep's note H): the random state the other copy built its floor from, when apart and it is on this dungeon's
// floor `floor` (0 up), so this copy, arriving second, builds the same floor; false otherwise.
bool CoopWorldsPeerFloorRand(int floor, std::uint32_t *rand);
