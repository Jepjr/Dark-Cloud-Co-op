#pragma once

#include "coop/game_ns.hpp"

#include <cstddef>

// Co-op turns at the Georama (game.coop): in a town's build mode one player builds at a time. Player 1
// starts with the editor; whoever has it clicks R3 to pass it to the other. While player 2 has it the
// game's first pad reads player 2's controller, so the cursor, the parts menu, the camera and the
// build-mode pause all follow player 2, and the second pad reads nothing; the debug reads (On2, Down2,
// the L3/R3 debug buttons) stay player 1's alone. Leaving build mode hands the editor back to
// player 1.
//
// Which player has the editor is game state: it changes only on the two pads every copy of a linked
// game agrees on (game.netplay) and on the editor's own mode, so linked copies stay in lockstep.

// Passes the editor on an R3 click. pad_button_read calls it once a tick, before it reads pad 0.
void CoopGeoramaTick();

// The player whose controller the game's port reads this tick: port itself, or -1 for none.
int CoopGeoramaSource(int port);

// Buttons the game's port reads as released: the ones the new holder held when the editor came to
// it, until they let go, so a button held through the handoff does not press on the first tick.
int CoopGeoramaHeld(int port);

// Whether player 2 has the editor now.
bool CoopGeoramaPartnerHolds();

// Draws whose turn it is. EditMainDraw calls it in build mode.
void CoopGeoramaDraw();

// coop-party: session reset: the editor back to player 1 (CoopResetForSession).
void CoopGeoramaResetForSession();

// coop-town: player 2's own Georama in a linked town (coop_georama_own.cpp). CoopP2GeoramaTick: each tick of the town,
// both copies, after the town's own mode step: her Select opens or closes it, and while it is open her editor steps.
// CoopP2Building: whether it is open now. Reset with the session (CoopResetForSession).
bool CoopP2Building();
// coop-georamabar: each town frame (one call a frame) whether it is one of the frames just after her Georama closed, while
// the help bar her editor set may still be up.
bool CoopP2HelpLeftFrame();
// Whether her parts menu is open or opening (the town's own menus wait meanwhile), and the mode her copy draws
// the frame in while her Georama is open (-1 when it is shut).
bool CoopP2MenuOpen();
// coop-stage4: her parts menu open holds his walk's menu (one engine), unless what he opens (menu_no, ED_MENU_*) and hers both run local;
// DC_COOP_MUTATE=p2georama_holds_his_menu (twin): any of his waits, as before.
bool CoopP2MenuHoldsP1(int menu_no);
// coop-p1parts: his parts menu run local (coop_georama_own.cpp). Whether her open parts menu holds his from opening; whether this copy leaves
// his out (EdInitModeFinish); a parts menu of either open local (every copy keeps EditSave's state alike); shut here and its command on the way
// (his copy draws his Georama meanwhile); his menu's step in the town's: 1 as it shuts on this copy's tick.
bool CoopP2MenuHoldsParts();
// coop-pt1108: a local menu's writes to the chip boards and the parts' records as pokes, around its step (collect) or draw (the overlay only).
void CoopPartsWritesBegin(bool collect);
void CoopPartsWritesEnd();
bool CoopP1PartsMenuElsewhere();
bool CoopPartsMenuLocalOpen();
bool CoopP1PartsMenuShutHere();
int  CoopP1PartsMenuStep();
bool CoopP1PartsMenuCloseFrame(); // coop-pt1108
// coop-stage4: sets the parts menu's close command's handler (kCmdGeoMenu), every copy (coop_fullmenu's FullMenuCommandHandlers).
void CoopP2GeoramaCommandHandler();
// Around the town's mode step: a part player 1 puts down on her puts her out of it.
void CoopGeoramaP1StepBegin();
void CoopGeoramaLeaveVillagers(float (*at)[4]); // coop-pt1109c: as a Georama ends, retail's placement for the villagers a new part blocks
void CoopGeoramaP1StepEnd();
// Around the town's frame on her copy while her Georama is open: her editor in the globals for its drawing.
// The twin's old rule (DC_COOP_MUTATE=p2georama_one_at_a_time): player 1's Select waits while her Georama is open.
bool CoopP2GeoramaHoldsP1();
// In a Georama frame, after this copy's own cursor (pass 1 and 2, as DrawPartsCursor): the other builder's hand cursor
// and the part it holds, from that editor's state. Drawing only (Jep's playtest, 2026-10-21).
void CoopGeoramaDrawOtherCursor(int pass);
// The other builder's hand (2D, beside ParamDraw's own); logs "coop-georama: pN drew player M's hand" once a second.
void CoopGeoramaDrawOtherHand();
void CoopP2DrawEditorBegin();
// Her Georama's view (her camera's matrix and eye) on her copy while it is open; false otherwise.
bool CoopP2DrawCamera(float (*view)[4], float *eye);
void CoopP2DrawEditorEnd();
int  CoopP2DrawnMode();
// Around the town's frame on her copy while her parts menu is open: it is drawn into a target nobody sees.
void CoopP2TownFrameBegin();
void CoopP2TownFrameEnd();
void CoopP2GeoramaTick();
// coop-stage4 4d: this copy's player builds apart, which holds no merge; parked as the link starts (not his as the stayer), and pressing its own
// Select once landed (OwnInput).
bool CoopGeoramaCarries();
void CoopGeoramaPark(int player);
bool CoopGeoramaParkedPress(int player);
// coop-stage4 r4: her Georama as the stayer goes on through the merge as her own (her copy, as the link starts), carried with the set.
void           CoopGeoramaHerToOwn();
unsigned char *CoopGeoramaSyncBytes(std::size_t *size);
void           CoopGeoramaSyncTake();
void           CoopGeoramaSyncClear();
void           CoopGeoramaSyncApply();
void           CoopGeoramaTestPickApart();
void CoopP2GeoramaResetForSession();
