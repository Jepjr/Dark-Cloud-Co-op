#pragma once

#include "coop/game_ns.hpp"

// The partner's Cross on doors, gates, chests and Atla, its Square on a gate that wants an item, its chest
// opening, and its pickups.

#include <cstddef>

#include "platform/input.hpp"

namespace dc::game {
class CDungeonEventData;
class CCameraFollow;
class CFrame;
class CCharacter;
} // namespace dc::game

namespace coop {

bool Interact();
bool InteractItem();
// coop-atlaapart: whether this copy's player takes an Atla as any member, not Toan alone as retail's (DunMoveChara's Cross).
bool OwnPlayerTakesAtla();
// coop-atlaswap: the game's player's own Atla (BtAtraGetShort_Init / _Loop) when that player is not Toan: PlayerAtlaPack, the
// pose pack its Init loads (her chest set's, retail's Toan pack for Toan); PlayerAtlaStance (step 0): stands her as her
// partner's Atla stands her (where she pressed, facing out the way the effect is turned to her, her chest set's standing
// grab) and gives the effect's turn, false for Toan (retail's pose); PlayerAtlaStep (step 1): her step back to retail's 9.4;
// PlayerAtlaEndTurn (step 5): the way she turns as it ends (retail's -PI for Toan).
const char *PlayerAtlaPack(const char *retail);
bool        PlayerAtlaStance(const float *ball, const float *stood, float *turn);
bool        PlayerAtlaPosed();
bool        PlayerAtlaPackFits(); // coop-atlaswap: the pose pack loaded is her own member's (step 0's load)
void        PlayerAtlaStep();
float       PlayerAtlaEndTurn();
void KeyDoorTest();
// coop-keyring: test hook (DC_COOP_KEYRING=stay|host), each dungeon tick, apart too: this copy's own player uses the floor's
// gate key from the party's ring at its lock (x-dungeon-p2 keyleave, x-floor-join late -KeyDoor).
void KeyRingTest(bool lines_only = false);

// coop-keypick: her own item picker at a slot that wants an item (a gate's lock), beside the running game
// (coop_interact.cpp). OpenKeyPick: her Square at such a slot (true: handled). StepKeyPick: each tick of her walk, on
// pad 2; true while it holds her (she stands). DrawKeyPick: on her copy, from CoopDrawWheel. KeyPickAnswer: where a
// slot's script asks for the Use Item window (DunMoveChara), her pick answers it without the window (true).
bool OpenKeyPick();
bool KeyPickOpen();
bool StepKeyPick(const InputPadState &pad);
void DrawKeyPick();
bool KeyPickAnswer();
// pt1111c: every player said yes to player's (1 up) item scene: its slot's script starts with the held pick (item, from the
// party's pack when shared) as the window's answer, as the picker starts any slot's (StartWithAnswer). Whether it started.
bool StartPickedItemScene(int player, int slot, int item, bool shared);
// pt1111c test hook DC_COOP_TEST_ITEM_SCENE (x-item-scene): stepped each frame.
void ItemSceneTest();
// coop-gatekeyno: at a floor's start, a line when the floor's gate key is not the one its lock's script asks for.
void LogFloorGateKey();
// Player 1's (Jep: "just like Xiao's game shouldn't pause when p1 is interacting with an item"): DunMoveChara's Square
// at a slot that wants an item asks OpenKeyPickFor(1, slot) (false: the window as retail's); MenuTick steps it on pad 1
// and MenuFilterPort keeps his pad from the game while KeyPickOpenFor(1).
bool OpenKeyPickFor(int player, CDungeonEventData *state);
bool KeyPickOpenFor(int player);
bool StepKeyPickFor(int player, const InputPadState &pad);
// coop-p2actions: what the player held as their picker closed (the press that closed it), kept from their game input until let go.
std::uint16_t KeyPickHeldAfter(int player);
void KeyPickResetForSession();
void KeyPickEndRunsForQuit(); // coop-xiaogap2: the session over (Quit Game): every run off, player 1's too
// coop-keydoor: the key doors opened beside the game (coop_interact.cpp) step on, each tick on a floor, from CoopStep.
void KeyDoorTick();
// coop-skeldoor: a key door refusing player's pick plays retail's refusal on that player, who stands still meanwhile (Walk
// holds her stick, MenuFilterPort his pad).
bool DoorRefusing(int player);
// coop-skeldoor: player 1's Cross started a script whose Cross only shows a message: it shows on his screen beside the
// game, the task back to play (CoopWorldsGateVote calls it as the script starts). False: the script runs as retail's.
bool HisCrossBeside(int script_no);
// coop-gateopen: a gate opened once stays open for both players (coop_interact.cpp). GateOpenTick: each dungeon tick,
// apart too, from CoopStep. GateOpenFromPeer: the other copy's kWhatGate progress. OpenGatesBytes: the table, for the
// snapshot's extras. GateOpenKnown: whether the table has it (tests' lines).
void GateOpenTick();
void GateOpenFromPeer(int dungeon, int floor);
bool GateOpenKnown(int dungeon, int floor);
unsigned char *OpenGatesBytes(std::size_t *size);
// coop-dungeonsuite: player 1's own presentations (a chest's, an Atla's, a gate key's or an attachment's pickup) stop only
// player 1: the partner walks and fights on through them (DunMoveChara's CoopStep). DC_COOP_MUTATE=presfreeze: frozen, as before.
bool PartnerPlaysThrough(int task);
// coop-p2hurt: the camera of the partner's gate key presentation while it plays, else null. Her copy draws from it
// (CoopViewBegin); his never does. (coop-opt7 step 4: her chest's and Atla's, coop-openercam's, are retail's own on her copy.)
CCameraFollow *OpeningCamera();
// Whether the partner's chest opening or Atla presentation is playing (the world's, every copy); DrawPartnerPresentation: her
// Atla's effect and her chest's glow and item where she stands (CoopDraw; coop-opt7: on the others' copies).
bool PartnerPresenting();
void DrawPartnerPresentation();
// coop-atlaopen: on the copy that shows her Atla, the light it casts on her body standing at stood (retail's Draw_MainUnit
// light01 point light) put into the game's light (MGGetPLight's), for her body's draw; false when there is none.
// PartnerItemDrawn: the scale her chest's item was last drawn at on this copy (0 none; the tests' line).
bool  PartnerPresentationLight(const float *stood, float light[4][4], float colour[4][4]);
float PartnerItemDrawn();
// coop-actorswap: her Atla's presentation is under way (she plays its pose; her walk leaves her motion alone).
bool  PartnerAtlaPresenting();
// coop-chestfix: her copy's opening box (retail's itemOpenSmall / itemOpenBig) over the floor's box she opens: Begin leaves the
// floor's box out of the frame's draw, End puts it back (CoopViewBegin / CoopViewEnd); PartnerBoxDrawn: the opening box's
// frame as last drawn on this copy (0 none; the tests' line).
void  PartnerBoxDrawBegin();
void  PartnerBoxDrawEnd();
float PartnerBoxDrawn();
// coop-b4story: on her copy through his chest's presentation (DrawDungeon's opening branch), the floor's boxes drawn from centre,
// his opening one left out (retail draws none there).
void HisPresentationBoxesDraw(const float *centre);
float PartnerGlowDrawn(); // coop-chestseen: her chest's glow's effect frame as last drawn on this copy (0 none)
float PartnerLightDrawn(); // her Atla's light's rate on her body as last drawn on this copy (0 none)
// coop-p2prompt: the prompts a locked or trapped chest's, the clown chest's, a message-only slot's and the exit gate's script
// would show, run for the player who used it beside the running game (coop_useprompt.cpp). Open*: true when it took the use.
// StepUsePrompt: each tick of that player's walk, on its agreed pad; true while it holds the player. DrawUsePrompts: on the
// user's copy, from CoopDrawWheel. EscapeWindowAnswer: the gate's escape window, answered already by the players' yes.
bool OpenChestPrompt(int player, int active, int entry_no);
bool OpenGatePrompt(int player, CDungeonEventData *gate);
bool CrossMessagePrompt(int player, int script_no);
bool ScriptMessagePrompt(int player, int mes); // coop-choosing: a door's script message beside the game, its user's screen only
bool ScriptChoicePrompt(int player);           // coop-choosing: that message as its question (the script set its cursor)
int  ScriptPromptChoice(int player);           // coop-choosing: the question's answer (0 yes, 1 no, -1 none yet)
bool ScriptPromptTestAnswer(int player, int row); // a test hook's answer to it (x-back-half -Leave)
// coop-choosing (ura, the back half of a floor): a player's use of a back-half door beside the game; the unlock table.
bool UraDoorStart(int player, CDungeonEventData *state);
bool CharaSpotStart(int player, CDungeonEventData *state, int member); // coop-xiaogap: a character spot's script beside the game, as its user
bool CharaSpotHis(int script_no); // coop-xiaogap: player 1's Cross at a character spot: the gap gate, else its program beside the game
// coop-xiaogap2: the gap's cutscene beside the game: her real jump on her body on every copy (SpotJumpPoseFor, before the body's
// step: motiondrive.cpp for player 1, the partner's step for player 2), the camera rig on the jumper's copy for its draw alone
// (SpotJumpCamera in CoopViewChoose; SpotJumpViewEnd puts the play projection back after the draw).
void           SpotJumpPoseFor(int player, CCharacter *body);
CCameraFollow *SpotJumpCamera();
bool           SpotJumpCameraOn();
void           SpotJumpViewEnd();
int            SpotJumpHudY(int y, bool map_shown); // the HUD's place for this draw (coop_fullmenu.cpp FullMenuHudY)
// coop-gapmark: whether an interaction mark ("!", which: whose, for the trace) is drawn on this copy now: none through this copy's
// player's own scene (ViewPrivateScene). Draw only.
bool           InteractMarkShown(const char *which);
bool UraDoorStartHis(int script_no);
void UraOpenTick();
void UraOpenFromPeer(int dungeon, int floor, int script, int item);
unsigned char *UraOpenBytes(std::size_t *size);
bool UraEnterAgain(int script_no, int slot); // the back half's door again on this copy alone, its question answered yes
void UraUnlocked(int script, int item);
std::uint16_t BoneDoorTestButtons(); // coop-choosing: a test hook's presses on the game's pad (DC_COOP_BONEDOOR_LATER)     // its unlock (its door's item use) on this floor, for every player
int  CrossMessage(int script_no);
bool UsePromptOpen(int player);
// coop-menucross: in a linked town, a door's message for her Cross while it is shut, as her own note (her copy alone);
// the notes' countdown there (CoopTownStep); the message a town door event's Cross shows while its flag is clear (-1: none).
bool TownNotePrompt(int player, int mes);
void StepTownNotes();
// coop-cavehold: a door's message that waits for its player's Cross (StepTownNote: each town tick on their agreed pad, true
// while it holds them), what that Cross left held (kept from the game until let go), and whether a note is up (the test's line).
bool          StepTownNote(int player, const InputPadState &pad, bool in_town);
bool          TownNoteHolds(int player);
std::uint16_t TownNoteHeldAfter(int player);
bool          TownNoteShown(int player);
void          TownNotesEnd();
int  TownDoorShutMessage(const void *program, int event_no, int *flag);
// coop-hischest: what the player held as its prompt closed (the Cross that closed it), kept from the game until let go.
std::uint16_t UsePromptHeldAfter(int player);
bool StepUsePrompt(int player, const InputPadState &pad);
void DrawUsePrompts();
// coop-keylook (Jep's own-UI rule: our boxes never over the game's UI; a duplicate looks like the original): a line or lines of the
// mod's own text (a note, a prompt's head and rows) in the game's own message window, the floor script's window 1 (EdEventInfo
// .messages[1]), as a chest's message is drawn, on that player's screen: slot 0-1 the players' prompts, 2-3 their key pickers' notes.
// head: the lines above the rows; cursor: the hand's row among the rows, -1 none. False when the game's window is not there (then
// the caller draws as before).
bool DrawTextInGameWindow(int slot, int player, int local, const std::string &text, int head, int cursor);
void DrawCallQuestion(); // coop-floorgate: the other player's call, apart
void DrawEntranceOverlays(); // coop-gatepopup2: the call's boxes and the notice over a dungeon's floor menu
// coop-floorgate: the dungeon menu's centre Yes/No popup (its Go Outside question's look, no gilda or Escape Powder panels): the
// question's lines in the help window, the Yes and No plates under it, the hand on cursor (0 Yes, 1 No). Draw-only, on this
// copy; changed: the text is new (laid out again). False while its pictures are not loaded (or DC_COOP_MUTATE=gatherbubble):
// draw the question another way then.
bool DrawYesNoPopup(const char *question, bool changed, int cursor);
// coop-swapbtn (test hook DC_COOP_TEST_PLATE_PLANT, coop_useprompt.cpp; game/texture.cpp's rebuild calls it): the popup's YES
// plate made at the key the registry hands out next; its key.
unsigned PlatePlantMake();
bool EscapeWindowAnswer(int *answer);
void SetEscapeWindowAnswer(int answer);
void UsePromptResetForSession();
// coop-dungeonreds: a player who leaves the floor (a floor's split, on every copy on its one tick) leaves their use prompt there:
// it closes, a gate's waiting question with it (the vote that ended in this split is over).
void UsePromptEndForLeaver(int player);
// coop-clownchest: her copy's roles swap at her floor merge (CoopWorldsFloorBecomePartner): the prompt she opened apart, as her
// copy's game player (player 1's slot there), goes on as player 2's. Said on stderr when one moves.
void UsePromptToPartner();
// (its twin's hooks around a state set's apply: DC_COOP_MUTATE=useprompt_not_carried keeps this copy's own prompts)
void UsePromptApplyBefore();
void UsePromptApplyAfter();
// coop-doorprompt: the same picker at Norune's cave door in a linked town, either player's Square there (CoopTownStep):
// stepped on both copies from its owner's pad (MenuTick), drawn on the owner's copy alone. The cave key picked there
// (ITEM_CAVE_KEY, which the door's event 130 asks for) gives its player once from TownCaveKeyUsed (0: none).
bool OpenTownDoorPickFor(int player);
bool TownDoorPickOpenFor(int player);
// Open, or closed on this tick (the Cross or Circle that closed it is not a door's: CoopWorldsPartnerMayStart).
bool TownDoorPickHeldFor(int player);
int  TownCaveKeyUsed();
// coop-p2marks: her "!" off (DC_COOP_MUTATE=nomarks, or the test's DC_COOP_P2MARK_OFF ticks); drawing it at a point (drawn
// false: nothing drawn this frame), with the once-a-second debug line. Town and dungeon (CoopTownDrawPartnerMark, CoopDrawPartnerMark).
bool PartnerMarkHeld();
void DrawPartnerMarkAt(CFrame *mark, const float *at, bool drawn);
// coop-fx51a: its log window starts again (a state file's load, coop_worlds CoopWorldsStateFileLoaded).
void PartnerMarkLogReset();
// coop-revive2: the red mark over a player lying down, on near alive players' screens (CoopDrawPartnerMark draws it).
void ReviveMarksDraw(CFrame *mark);
// coop-revivecross (the developer's rc5b playtest, 11-19: "P2 cant open chests anymore" while P1 lay down): the one rule for the revive's
// offer, which both the red mark (ReviveMarksDraw) and the Cross (coop_party StepDown, coop_combat PlayerDownStep, her Cross
// order) read: reviver (1 up) is up, downed lies down within kReviveDistance of it, and no retail target its Cross would act on
// (InteractTargetDistance) is nearer than the body (retail's own Cross takes the one interaction in reach; with two, the nearer).
// A small chest's reach is 8 and the partner keeps 10 from player 1 (KeepApart), so in practice a chest in reach wins.
// DC_COOP_MUTATE=revive_over_target (twin): offered whenever near, a nearer chest or door ignored.
bool  ReviveOffered(int reviver, int downed);
const char *ReviveOfferTrace(int reviver, int downed); // the rule's parts, for a debug line
int   ReviveOfferedAny(int reviver); // the player it is offered over, 0 none
// Whether player's Cross this tick is the revive's (offered over someone): its Cross then starts no script, chest or swing (player 1's
// in DunMoveChara, the others' in their Cross order). DC_COOP_MUTATE=revive_swings (twin): the Cross acts as well, as before.
bool  ReviveTakesCross(int player);
// One Cross, one action: the step that reads a player's Cross first (player 1's DunMoveChara, the others' Cross order) says
// what it went to (the revive, or the game's own target or swing); the revive's question (coop_prompt ReviveAsk, later in the
// same tick, after a chest it opened is gone) takes that word for the same press. Read once (ReviveCrossTaken: 1 the revive's,
// 0 not, -1 no word). DC_COOP_MUTATE=cross_twice (twin): no word, the revive judged again, as before.
void  ReviveCrossDecided(int player, bool revive);
int   ReviveCrossTaken(int player);
// A swing started by player's Cross (DC_COOP_DEBUG: "coop: P<n> starts a swing", the revive tests' word).
void  NoteSwing(int player);
// What reviver's Cross would act on where it stands, as DunMoveChara and Interact find it (an event slot whose script its Cross
// starts, else the map event in reach): its distance from the body, -1 none.
float InteractTargetDistance(int player);
// coop-revivecross: player 1's action lock (BtActStatus can_act / movement_locked) as it holds player's own Cross and Square on a
// floor. Player 1 lying down holds both every tick (PlayerGoesDown, PlayerDownStep, DunMoveChara): that is his body's, never the
// other players'. DC_COOP_MUTATE=down_holds_her (twin): his down holds hers too, as before.
bool  HisActionHolds(int player);
// coop-opt7 step 3 (debug\notes\opt7-design.md): the partner's body in a presentation of her own, its world half, on every copy
// alike (coop_opener.cpp's PresentStartFor / its tick / kCmdPresent call them; her copy alone runs retail's presentation as its
// engine). Start: she stands at the box and plays its opening (posed false: refused, she stands where she is), or takes her
// stance at the ball (turn: the effect's toward her). Step: her pose by the presentation's ticks (and, on the other copies, her
// chest's glow and item and her Atla's effect, draw only: coop-chestseen, coop-actorswap). End: she stands back as retail's end.
// PartnerCheckItemGet / PartnerGetItem (coop-inventory): her own bag's rule and take.
void PartnerPresentChest(int entry, bool big, int item, bool posed, bool others);
void PartnerPresentAtla(int entry, bool others, float *turn);
// coop-pt1111b (item 12): any opener's Atla effect on the ball on a copy that is not the opener's, from the press to the
// presentation's end (the world's tick steps it; draw only). DC_COOP_MUTATE=atlafx_partner_only (twin): player 2's alone.
void OthersAtlaFxStart(int opener, int entry, float turn);
void OthersAtlaFxStep(int opener);
void LogAtlaFly(int opener, bool here, int tick); // coop-atlafly test hook (DC_COOP_TEST_ATLA_FLY)
// coop-atlafly: after a step of an opener's Atla effect (retail's, or the others' draw of it), its light balls carried to come down
// against the opener's body as retail's do against Toan's (no change under retail's own pose). opener: the player number (0 none).
void AtlaFlyToOpener(dc::game::CCharacter *fx, int opener);
void OthersAtlaFxEnd(int opener);
bool PartnerPresentStep(int kind, int ticks, bool others); // true on the tick she holds the item up
void PartnerPresentEnd(int kind, bool posed);
bool StepBusy(const InputPadState &pad);
void PoseBusy();
void PartnerFramePlace(float *at); // coop-openpos
void PickUp();
void PartnerStealArrives(); // coop-stealback: her arrived stolen items on her own step

} // namespace coop
