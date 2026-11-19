#pragma once

// coop-faces (Jep 11-18, facial plan option A + C): faces that move on every player's body. Retail moves a face only by copying
// strips of a face sheet (the member's eyes' and mouth's frames) over the face picture painted on the head (textureanime.cpp),
// and in play only Toan's town model does it (his blink). Every co-op body now blinks with its own member's frames from the disc:
// the other players' town and dungeon bodies, stand-ins, player 1's own dungeon body (retail never blinks anyone there), and a
// fisher drawn as their own member shows the look the fishing script puts on Toan's hidden fishing model, mapped onto their own
// face sheet (FacesLookTable). Each copy draws its own: nothing is sent and nothing of it is in the state set or the sync check.
//
// game.coop_faces (config.json, each player's own, default on); false: retail's faces exactly (nothing here runs).
// DC_COOP_TEST_FACES=1: a test line per face kit each 30 draws (coop-faces: test: ...).
// DC_COOP_MUTATE=faces_anyblock (twin): the kit finds its two pictures by name in any block (TexManager.GetTexture(name, -1),
// retail's config loader's lookup), the trap the plan names: another body's same-named picture can be the one found.

namespace dc::game {
class CCharacter;
}

namespace coop {

// game.coop_faces (and not DC_COOP_MUTATE=faces_off).
bool FacesOn();

// Each member's face sheet and face configuration read from the disc once (a few hundred KB, kept): called where bodies load,
// so a draw never waits on the disc. FacesDraw does it itself the first time if no load did.
void FacesPrepare();

// After body->TextureAnime(block) and before its Draw: its own face kit steps (built, or built again after its block changed, in
// the body's own block). fisher: the player (0 up) whose fishing this body shows (a fisher drawn as their own member), or -1 (a
// fishing model, c01d_turi, is found by itself). whose: names the body in log lines.
void FacesDraw(dc::game::CCharacter *body, int block, int fisher = -1, const char *whose = "a body");

// Jep's 11-18 pick (option 1): a fisher's look from their fishing state (ebattle.hpp EdFishingState), as Toan's fishing look T0-T8:
// a fish at the bait or hooked T3 (surprised), fighting it T4/T5 in turn (gritted teeth), landed T7 (surprised 2), the bait lost T2
// (side look), else T0 (the blink). draws: the body's face draws, for the turns.
int FacesFishLook(int state, long long draws);

// The member's face group the fishing look (Toan's fishing model's group, 0-8) shows: the blink group for none or no match.
int FacesLookFor(int member, int toan_look);

// Tests: every face kit built again at its next draw (coop_town's DC_COOP_TEST_FACES_THIRD, after a body of the same member loads).
void FacesRebuildForTest();

} // namespace coop
