#pragma once

#include "coop/game_ns.hpp"
#include <cstddef>
// coop-scriptmenus: the menus an event's script opens, on the opener's copy alone (coop_scriptmenu.cpp). Called from the event's
// step (ps2/src/editloop3.cpp EdEventMode) in place of retail's EdInitModeFinish, EdMenuMode and EdExitMenu there.
namespace dc::game {
class CCamera;

void CoopScriptMenuOpens(int kind);       // every copy, as the script asks for its menu (ED_MENU_*): whose it is
int  CoopScriptMenuInit(CCamera *camera); // every copy, its opening's frames: true on the fourth
int  CoopScriptMenuStep();                // every copy, each tick it is open: true on the tick it ends
int  CoopScriptMenuResult(int here);      // the result the script gets (here: retail's own)
void CoopScriptMenuExit();                // after the script took it
void CoopScriptMenuOver();                // its opening refused
bool CoopScriptMenuOpenHere();            // a script's menu is open and made on this copy
} // namespace dc::game
void CoopScriptMenuCommandHandler();      // kCmdScriptMenuEnd's handler, set once per copy
// coop-small2: a script's menu open at a merge goes on local on its opener's copy; its record travels with the state set
void           CoopScriptMenuRetailToLocal();                   // the sender, before its set is taken
unsigned char *CoopScriptMenuSyncBytes(std::size_t *size);      // the state set's extra
void           CoopScriptMenuSyncTake();                        // the sender, as its set is taken
void           CoopScriptMenuSyncClear();                       // the taker, before the apply
void           CoopScriptMenuSyncApply();                       // the taker, after it
