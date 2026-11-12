#pragma once

#include "coop/game_ns.hpp"

#include "platform/input.hpp"

#include <string>

// The co-op link (game.netplay 1 or 2): two copies of the game run the same co-op game in lockstep.
// Every tick each copy reads its own controller (player 1's copy controller 1, player 2's controller
// 2), sends it to the other over UDP and waits for the other's for the same tick, so both copies feed
// the game the same two pads and simulate both players alike. Inputs take effect game.netplay_delay
// ticks after they are read. Every second the two compare a checksum of the game's state and log a
// warning, and show one, on the first tick they differ.
//
// Menus: the game's menus read controller port 1. Normally that is player 1's pad, and player 2's pad
// steers the partner. When player 2 presses Start on the floor while player 1 presses neither Start
// nor Triangle, player 2 takes the menu: port 1 reads player 2's pad, so the pause follows player 2,
// and the partner stands still until the game is back under control on the floor, when port 1 is
// player 1's again (at once if the pause never opened, or an event or chest started on that tick
// instead). Pad 2's Triangle is the partner's own item panel (coop_combat.cpp), which does not pause.
// Both copies decide this from the pads they agreed on, so the menu is the same in both.

// Exchanges this tick's inputs. pad_button_read calls it when the game reads its first pad.
void NetplayTick();

// coop-snapshot: the main loop's frame start, before the mode's frame (stage 1's round trip takes and puts back the
// state here, where no game frame is on the stack).
void NetplayFrameStart();

// Whether the link is up, and if so the pad the game's port reads this tick.
bool NetplayPad(int port, InputPadState &out);

// coop: cameras
// The player this copy plays while the link is up (1 or 2), else 0.
int NetplayLocalPlayer();
// coop-soundpt (N players): whether this copy plays as one of the players (NetplayLocalPlayer() not 0: linked, or a host
// recording for a catch-up), whichever player that is.
bool NetplayHasPlayer();

// coop-xiaoback: whether the partner plays in lockstep now (the link up and no catch-up recording or replay). Unlike
// NetplayLocalPlayer, a host recording its solo play for a catch-up reads false: nobody has joined yet.
bool NetplayPartnerLinked();

// coop-worlds: whether the link is up (a catch-up's replay and the lockstep alike), not merely configured.
bool NetplayLinkUp();
bool NetplayStateLoadPending(); // coop-cavedoorfix: a state file still to be put back (the test hooks wait for it)

// coop-partnerlog: the player this copy linked as (1 or 2) since its link first began, linked or apart; 0 before.
int NetplayLinkPlayer();

// coop-party: which player holds the game's menus (1 or 2) while the link is up, else 1.
int NetplayMenuOwner();

// The --input script's frame for this boot frame: frames since the first link with DC_NETPLAY_TEST_INPUT_FROM_LINK=1
// (0 before it), else the boot frame.
std::int64_t NetplayTestInputFrame(std::int64_t frame);
// The event a held --input script waits for (DC_TEST_INPUT_HOLD="F:event[,F:event...]"), marked by the game ("load": the
// title's load screen opened; "lobby": the lobby shown; "linked": the first link).
void         NetplayTestInputMark(const char *event);

// The menu presses' test clock (DC_COOP_MENUPRESS): ticks since the pair's free play began, -1 before.
std::int64_t NetplayTestPressClock();
// coop-keydoor (tests): the tick the last catch-up's lockstep began on, alike on both copies (-1 before one); and this copy's
// frame to path at the end of the next frame (as DC_COOP_MENUSHOTS writes its frames).
std::int64_t NetplayTestLockstepFrom();
void         NetplayTestShot(const char *path);
// coop-houseoutframe (tests): a frame to a file is due at the end of this frame (its draws are the frame's).
bool         NetplayTestShotPending();
// coop-localfix2 (test traces): the link's tick now.
std::int64_t NetplayLinkTick();
// coop-gatekey52 (tests): whether a state file (DC_STATE_LOAD_AT's fixture, a serve job's) has been put back on this copy.
bool         NetplayStateFileLoaded();
// coop-warmconvert (WS7 round 4): whether a state file was put back since this job began on a warm pair (its fixture), or at
// all on a copy with no job: a "loaded" test hook fires on the job's own load, not on the pair's first.
bool NetplayJobStateFileLoaded();
bool         NetplayStateFileApplying(); // coop-townstand: a state file (a fixture) is being put back now
// coop-gatekey52 (tests): and the link's first checksum compare after it is past (both copies' sums taken: an in-sync report
// can come for it, which a split on the load's own tick never leaves room for).
bool         NetplayStateFileChecked();
// coop-padswap test hook (DC_COOP_TEST_PAD_PROBE): arm the probe at her room event's start; its phase now (1 his stick
// alone, 2 hers, 3 a pause, 0 none).
void NetplayPadProbeArm();
int  NetplayPadProbePhase();
// coop-closetitem test hook (DC_COOP_TEST_MES_PRESSES): arm at a player 2 message that waits for a button; its phase.
void NetplayMesPressArm();
int  NetplayMesPressPhase();

// coop-fullmenu: the pad player (1 or 2) gave this tick, as the copies agreed on it; false while not linked.
bool NetplayPlayerPad(int player, InputPadState &out);
// coop-atlacross: whether player (1 or 2) pressed any of buttons this tick on the agreed pads (down now, up the tick
// before), whoever holds the menus; false unlinked.
bool NetplayPlayerPressed(int player, std::uint16_t buttons);

// Whether the controller ports belong to the link: game.netplay is set and the link is up or not yet
// tried. read_pad (port/src/coop/netplay_pad.cpp) then reads both ports every tick, whatever this PC
// has plugged in.
bool NetplayOwnsPads();

// coop-p2absent: whether this copy is one of a link (game.netplay set: Online host or join, Linked),
// up or not. Player 2 is then only ever the link's, never a controller plugged into this PC.
bool NetplayConfigured();

// A joiner catching up to the host (game.netplay_catchup) with more of the host's pads held: its ticks run as fast as
// the game can, not waiting for the next display boundary.
bool NetplayCatchingUp();
// A joiner replaying the host's pads, from the replay's tick 0 to the lockstep (waits for the next blob included).
bool NetplayReplaying();
// coop-atlamenu: a catch-up's ticks that step alike on both copies (the sender's from its state set's take, the taker's replay).
bool NetplayCatchupStepsAlike();
// coop-floorsnap: a catch-up join under way on this copy, host or joiner (from the link's start to the lockstep).
bool NetplayCatchUnderWay();
bool NetplayCatchWaitsToWalk();
bool NetplayMenuPad(int player, InputPadState &out); // coop-stage4 4c
int  NetplayGamePadPlayer(); // coop-scriptmenus: whose agreed pad is the game's (port 0) this tick
// coop-catchupdrop: a catch-up's pieces under way now (the sender's blob not yet all held, or the taker's last piece within
// kCatchMovingMs); false while it waits on the other copy (a page request, a landing). The partner log goes then.
bool NetplayCatchMoving();
// coop-floorsnap: this copy is a joiner that has put the host's snapshot in and replays from its tick (until the lockstep).
bool NetplaySnapshotApplied();
// coop-floorsnap: whether a join by snapshot is possible on this copy (a fixed-layout build, its arenas pinned: see
// SnapshotLayoutPinned). Off it, the worlds module merges on no floor and a join catches up as before.
bool NetplaySnapshotAvailable();
// coop-worlds: while this copy listens, the other copy's link traffic came in the last 2 s under the session its Hellos give:
// it has started a link (the merge's) and waits on this copy, whatever its last presence said.
bool NetplayPeerLinking();
void NetplayHelloSaysStayer(); // coop-stayerhello: a Hello goes now if the last one gave another stayer bit than this copy has
void NetplayHelloNow();        // coop-areacard: a Hello goes on the next listening pass (the merge's ready word changed)
std::uint32_t NetplayPeerHelloSession(); // coop-linkrace: the other copy's Hello's session (test hooks)
// coop-session: the other copy's last Hello says it has never linked (a copy started anew, joining from its title).
bool NetplayPeerFirstLink();
// coop-p2skin: player 2's copy, linked but before the host's first state (a join from the title, Local's included), steps
// its own game to the resync, Toan and all, then flashes black for the resync's load (Jep, 2026-10-24: "P2 still shows up
// as Toan before P1 loads in", "the game flashes black and catches up"). Its window holds the frame it came from until the
// host's state has been taken and the copy has left for the host's place (the tick after). GameRenderTick asks once a
// tick. DC_NETPLAY_MUTATE=show_join (twin): those ticks shown, as before.
bool NetplayHoldsJoinFrame();

// Desync recovery: when the checksums part, the host (player 1) picks a tick two seconds on; there it
// sends its random generator, SaveData (the party status among it) and where it is, the joiner waits
// for all of it, and both copies take it and leave for the host's dungeon, at its entrance, or town,
// where both start alike. The HUD then shows "Resynced at tick N". Whether a resync has just been
// taken, and if so the map both copies now leave for (NextMapNo's numbering); the game's main loop
// asks after each tick's pads and leaves its mode (port/src/gameloop.cpp).
bool NetplayTakeRestart(int &next_map);

// coop-mergetime: milliseconds since this copy last held a resync's state, -1 before any (the area card's log line,
// port/src/nowload.cpp).
long long NetplayResyncAgeMs();

// After a mode's loop has ended (port/src/gameloop.cpp): a resync's state is taken then, before the
// next mode starts.
void NetplayModeEnded();

// coop-snapshot (the floor-start checkpoint): a dungeon floor started on this copy,
// the party on it under control (the gate's or the entrance's script's "go dungeon", port/src/dun/movechara.cpp). A host
// recording alone takes its checkpoint there, so a joiner lands on that floor and replays from its start, not from the
// dungeon's entrance; a copy that took such a checkpoint starts its catch-up there.
void NetplayFloorStarted();
// coop-floorsnap: a dungeon floor about to be built on this copy (the script's _SET_RANDOM_MAP, before the build): a
// joiner catching up to that floor's start builds it from the host's random state, and every copy notes the state it
// built from, for its floor checkpoint.
void NetplayFloorBuilding();
// coop-localfix: the next frame drawn goes to path (tests).
void NetplayTestShotTo(const char *path);
// coop-framecap: writes a test frame under this copy's cap (DC_COOP_TEST_SHOT_MAX / _MAX_MB); false past it or on error.
// coop-shotthread: same_as, a frame already written with these very pixels: a hard link to it (a copy where links fail).
bool NetplayTestWritePng(const char *path, const std::uint8_t *rgba, std::uint32_t width, std::uint32_t height, const char *same_as = nullptr);
// coop-herexit2: DC_COOP_TEST_SHOT_SERIES_MARK's frames count from here on (tests).
void NetplayTestSeriesMark();
// coop-localfix: the random state this copy's floor was built from (its presence carries it while apart).
std::uint32_t NetplayFloorBuildRand();
// coop-localfix: the dungeon and floor (0 up) this copy built and is on (its scene included); false elsewhere.
bool NetplayFloorBuilt(int *map, int *floor);
// coop-cutscenejoin: how many floors this copy has built (a count, to tell a build after some moment from an older one).
int NetplayFloorBuilds();
// The floor (0 up) the entrance's window goes straight onto, once, after this copy took a floor-start checkpoint
// (GameTakeJumpFloor, port/src/gameloop.cpp); -1 otherwise.
int NetplayTakeRestartFloor();

// coop-housemerge: a building's interior set up on this copy (EditInInit's end, CoopWorldsInteriorStarted). The host alone
// inside (it went in apart) takes its checkpoint there, so a join or a merge inside catches up from the building's
// start; a joiner catching up to such a checkpoint takes the host's state again there and starts its replay.
void NetplayInteriorStarted();
// coop-housemerge: a joiner catching up to a building's checkpoint, on its first town walk after the checkpoint's town
// loaded: true (once) with the door the host went in by, GotoInterior's arguments and the jump id its door set, for the
// town's walk to go in by it (CoopWorldsReenterBuilding).
// coop-housemerge: the link up by a catch-up join (its lockstep begun): no load follows it on either copy.
bool NetplayLinkedByCatchup();
// coop-catchuprace: the host, linked, its catch-up's last blob made (it names J) or the lockstep from it begun: a change of
// player 2's that reaches it now rides no blob to her replay, so it goes into the lockstep as a command (both copies, one tick).
bool NetplayCatchLastBlobGone();
// coop-steamsplit: this copy plays with catch-up (game.netplay_catchup or DC_NETPLAY_CATCHUP), whoever sends.
bool NetplayCatchupSetting();
// coop-localfix: and the join went by the host's snapshot (the joiner landed where it stood, with no load of its own).
bool NetplayLinkedBySnapshot();
// coop-drawtwice (tests): the lockstep checksum now (its parts on stderr with DC_SNAPSHOT_TRIP_PARTS), for the draw-twice self-test.
std::uint32_t NetplayTestChecksum();

// coop-localfix2: a merge's link going by player 2's state (she stayed in the town, player 1 arrives), from its first tick to
// the lockstep, on both copies alike: she plays as the partner from her place checkpoint on.
bool NetplayStayerMerge();

// coop-ws5left (t-p2-houseout-view's merge-frame waits): running totals of where this copy's time went, in ms (the netplay
// tick, its wait for the other's pads, the state set's take, the catch-up host's tick, the sends). coop-town says a long
// frame wait's share of each beside its stamp; the rest is the game's own frame.
struct NetplayCostTotals {
    double tick_ms = 0.0, wait_ms = 0.0, take_ms = 0.0, catch_ms = 0.0, send_ms = 0.0;
};
NetplayCostTotals NetplayCosts();
std::string NetplayTickPartsSay(); // coop-small1b: the netplay tick's dearest parts and the log writes since the last call

// coop-localfix2 (Jep, 2026-10-26: "the catch up time is entirely hidden by the zone transition"): this copy arrived in a
// town where the other stayed and is to take her state: its window holds black (its door's fade) until it has caught up.
// DC_NETPLAY_MUTATE=arrival_shown (twin): its own town and then its last list, as before.
bool NetplayArrivalHold();
// coop-pt1111h: whether the last presented frame was in an arrival's black (NetplayArrivalHold's answer then; no side effects).
bool NetplayArrivalBlack();
bool NetplayCatchupBuildingDoor(char *name, int name_size, int *entrance, int *jump, int *start_event);

// What the HUD shows of the link: empty when off, else a short status; desync is set once the copies
// have differed.
const char *NetplayStatus(bool &desync);


// Player 2's Join on the title's main menu, when the online launchers ask for it (DC_NETPLAY_JOIN_MENU=1 on
// player 2's copy): the game boots to its menu as usual, the menu shows "press SELECT to join", and the link
// starts only once it is picked, listening for the host without stopping the menu; when the host answers, its
// state brings this copy to the host's place. TitleLoop passes whether Select was pressed on its menu; true
// while a join is under way (the menu then holds off its attract movie). TitleDraw draws the line.
bool NetplayTitleJoin(bool pressed);
// One client (game.netplay 3): whether the main menu's address entry is open (or the buttons that closed it are
// still held), so the title menu leaves the pad alone.
bool NetplayTitleEntryOpen();
// This copy's player in the link, 1 or 2 (one client: 1 until Join is picked), 0 unlinked.
int NetplayRole();

// One client (DC_NETPLAY=3, the LAN pack): SDL reads Xbox pads over raw HID (its HIDAPI and RawInput drivers), not XInput,
// before SDL starts. With Steam Input on, Steam leaves a game it did not start nothing on XInput once the game runs as
// Spacewar; raw HID still reads the pad (Jep's 2026-10-21 test). The launcher sets the same; its variables win.
void NetplayPadHints();
// coop-cand58: netplay's own threads stopped and joined before the game's teardown (main, as RunGame returns; once).
void NetplayStopThreads();
// coop-cand58 (tests, DC_NETPLAY_TEST_EXIT_HOLD_MS): says how many sends netplay's threads made after the stop.
void NetplayExitSaySends();
void NetplayDrawTitleJoin();
// coop-natsplit: why the last join from the title was given up, for 30 s after ("" otherwise): the menu's hint line shows it.
const char *NetplayTitleNote();
// Where the title's notice lines go (screen rows of the 640 x 480 2D layer): line 0 and the line under it, below the
// trademark lines (they end at kTitleTrademarkBottom); title_mpmenu's submenu hint takes the same places.
constexpr int kTitleTrademarkBottom = 436;
float         NetplayTitleNoticeY(int which);
// Whether a dark band goes under them: only where they sit over the copyright line (the old place).
bool          NetplayTitleNoticeBand();

// coop-worlds: the copies go apart (coop_worlds.hpp): the Leave goes out, then the link closes as a drop
// does ("netplay: link down at tick T: went apart (why)"), each copy playing on alone. mover: the player who left (its
// notice names them on both screens; 0 when no one did).
void NetplayGoApart(const char *why, int mover = 0);

// coop-apartsync: the other copy said it was quitting (its Leave), since the link last began: a link that went down without
// it is a drop (the network, or the other copy stopped answering), not a player leaving.
bool NetplayPeerQuit();

// coop-quittitle (Jep's rule, 2026-11-10): Quit Game from the in-game menu. Whether this copy is in a netplay session (a link
// started: hosting, joining, linked, apart or dropped), where the quit goes as a command (kCmdQuit, netplay_cmd.hpp) instead
// of upstream's own title return; false for single player.
bool NetplaySessionOpen();
// The game's main loop, at each frame's end: true once when this copy's session ended by a quit (the host's: every copy on
// the same link tick; another player's: that player's copy alone). The Leave is said, the link closed and the session reset
// to a booted copy's (it may host or join again), and the caller goes to the title (ReturnToTitle).
bool NetplayTakeQuitToTitle();

// coop-worlds: the host applied player 2's progress from apart (5 ints: what, a, b, c, d) while it records for a catch-up:
// the recording keeps it at this tick, so a joiner's replay applies it on the same tick (CoopWorldsApplyProgress).
void NetplayCatchupDelta(const std::int32_t delta[5]);

// The join or leave notice at the top left, if one is showing (draw only; the town's and the floor's HUD call it).
void NetplayDrawNotice();

// The title menu's Multi-player row (title/title_mpmenu.hpp), with one client (DC_NETPLAY=3, the LAN pack) before a
// game is hosted: whether it shows; Join > IP or Steam opens what SELECT opens (the address entry, or with Steam the
// friend search; NetplayTitleEntryOpen while it is up); Host > IP (1) or Steam (2) is how starting or loading a game
// then hosts (IP only: no Steam lobby; 0 as before: both). The line under the menu says which.
bool NetplayTitleMenuShown();
void NetplayTitleOpenJoin(bool steam);
void NetplayTitleHost(int via);
// coop-upnp: Host > UPnP is NetplayTitleHost(6) (IP, the router asked to forward the port: netplay_upnp.hpp); Join > IP's
// entry takes "address:port" too (the port the host's router forwarded, when not the usual one).

// Multi-player > Host or Join > NAT > Code (role 1 or 2; 0 forgets it): online for the session, the code entry open
// (NetplayTitleEntryOpen); hosting, player 1's socket opens now and STUN gives the code to send. A friend's code taken
// (NetplayTitleNatReady), the host punches towards it until the link is up; a joiner's taken code is its Join.
void NetplayTitleNat(int role);
bool NetplayTitleNatReady();
// Multi-player > Host or Join > NAT > Steam: Steam trades the two codes (the lobby), play goes direct.
void NetplayTitleNatSteam(int role);
// coop-lobby: the host's lobby (coop_lobby.hpp) listens for guests: player 1's socket, STUN for the outside address and the
// code, UPnP for the port. The code once STUN (or UPnP) has answered, "" before; the port the lobby listens on; whether this
// host asks the router by UPnP (game.netplay_upnp, DC_NETPLAY_UPNP).
void        NetplayLobbyListen(bool outside = true);
// The controller that walked the title menu (this copy's player 1), for the lobby's Start on another controller.
int         NetplayActivePad();
// coop-rumble: the controller this copy's player plays (OwnPad), as this copy numbers it; -1 none (not linked, or not
// claimed yet).
int         NetplayOwnPad();
// coop-lobbyleave: a guest lobby's own controller this frame (the copy's player 2 pad, as its join reads it), for its seat.
InputPadState NetplayLobbyOwnPad();
// The host's lobby keeps this copy on one controller (pad, from its open), not the one client's controller last pressed,
// which another local seat's controller took in the lobby (LAN pack 7, 2026-11-01); -1 (the lobby closed back to the menu)
// hands it back. A started game keeps it.
void        NetplayLobbyPinPad(int pad);
// The lobby started a window (a second copy) for a player on this PC: this copy keeps host_pad as its own from now on.
void        NetplayLobbyStartedWindow(int host_pad);
// The lobby's start hands a Multi screen seat on this PC to its own window (Jep, 2026-11-01): once this copy hosts, a copy
// starts for that seat, joining as identity on controller pad alone; this copy keeps host_pad.
void        NetplayLobbyHandOff(int seat, int host_pad, int pad, std::uint64_t identity);
const char *NetplayLobbyCode();
int         NetplayLobbyPort();
bool        NetplayLobbyUpnpWanted();
// coop-lobby: a join picked and waiting for its host (a lobby's guest until the game starts); the join given up from the
// lobby (Leave, the host closed it or went silent), with a note for the title's line or nullptr.
// coop-lobby: the host's Friend's code taken (false: it does not decode), and the address its punch goes to ("" none).
bool        NetplayLobbyFriendCode(const char *code);
const char *NetplayLobbyFriend();
// coop-lobby: this copy's lobby identity (game.netplay_player_id, made once), carried in its Hello and the lobby's messages.
std::uint64_t NetplayPlayerId();
// coop-lobby: the host's Select in the lobby, Steam's friends' lobby on or off (true: it is open); each lobby frame while it
// listens, the code and the seat table (opaque bytes) on it.
bool        NetplayLobbySteam(bool on);
// The lobby's start with Steam listening: Steam kept for the game as NAT > Steam's host (its lobby reopened by the game).
void        NetplayLobbySteamToGame();
void        NetplayLobbySteamTick(const void *seats, int size);
// coop-lobby: the address entry's Square asked for JOIN > STEAM's friend list (taken once; the title menu opens it).
bool        NetplayTitleTakeSteamList();
bool        NetplayLobbyJoining();
bool        NetplayLobbyServesJoin(); // coop-joinlobby: the host's game takes a join (listening, not linked)
// coop-titleopen51: the test's stop file seen (coop-qastop's DC_QA_STOP_FILE), for a twin that waits inside one frame
bool        NetplayQaStopSeen();
void        NetplayLobbyLeaveJoin(const char *note);
// coop-worlds: show text as that notice (the other player's news while apart).
void NetplayShowNotice(const char *text);
// coop-floorgate: the game flag floor `floor` (0 up) of dungeon `map` sets as its opening scene plays (MarkFloorScenesSeen's
// list), or -1 for a floor with none.
int NetplayFloorSceneFlag(int map, int floor);
// coop-b4story, pt1111c: the first game flag of floor `floor`'s story that is still unset: its opening scene's
// (NetplayFloorSceneFlag), else an event's of the floor (a walk-in marker's, a monster's death: kFloorStories, e.g. the cave's
// floor 8, 25); once all are set, the first of them; -1 for a floor with no story. DC_NETPLAY_MUTATE=midfloor_story_off (twin):
// scenes only; walkin_story_off (twin): no walk-in marker's.
int NetplayFloorStoryFlag(int map, int floor);
// coop-floorgate: this copy (apart) leaves wherever it is for dungeon map's floor (0 up), straight onto it from the entrance.
void NetplayGoToFloor(int map, int floor);
void NetplayGoToTown(int map); // coop-gateflow: back to a town from a floor menu (as a resync leaves)
// coop-saveload: player 1 loaded a save from his menu: the host's next resync takes both copies to it.
void NetplayMenuLoadResync();
// coop-loadpull: player 1 loaded a save apart: his recording from before it is dropped.
void NetplayLoadPull();
// coop-joinbudget: under DC_COOP_DEBUG, a line with this copy's clock (LogMs) for a step of a merge's link (the join's time before its link)
void NetplayTiming(const char *what);
