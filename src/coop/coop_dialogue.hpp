#pragma once

#include "coop/game_ns.hpp"

namespace dc::game {
class ClsMes;
} // namespace dc::game

// coop-dlgname: the speaker's name in partner-driven dialogue (playtest note 2, 2026-10-25: "Opening cutscene with mayor
// says Toan specifically instead of Xiao when P2 is talking to him"; Jep's yes, 2026-10-25). While the game's dialogue on
// this copy is player 2's (CoopWorldsPartnerSpeaks: her copy apart, or the town event she started), a line of a talk
// file (the town's or building's EditMes1 file, a floor's DngMes1 file) that calls the player by name (the name code
// for member 0, which the game fills in from the save: "Hey! Toan!") calls her by her member's name from the save.
// Menus, system text and everything else keep the game's own. DC_COOP_MUTATE=no_dlgname: the player's name as before (twin).
//
// ClsMes::GetTextLineDataTop gives its line's text here, text being what it found in mes's file (null for none), and
// returns the text to show: text itself, or the line with her name in, kept until the line or speaker changes.
short *CoopDialogueLine(const ClsMes *mes, int line, short *text);
