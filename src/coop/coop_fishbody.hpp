#pragma once

#include "coop/game_ns.hpp"

namespace dc::game {
class CCharacter;
} // namespace dc::game

// coop-xiaofish (Jep 11-16: each player fishes as their own character): retail has one fishing model, Toan's (c01d_turi), with the
// only fishing motions. A fisher who plays another member keeps that model for the game's fishing (its motions, its rod, float and
// hook, everything retail's code reads), but what is drawn is their own member's body in Toan's pose, the rod in its right hand.
// Nothing is made ahead or kept in the repo: the pose is worked out in the game from the player's own disc data, bone by bone
// through a bone map (coop_fishbody.cpp; the same sums as debug\tools\anim\fishbody.py, which checks them by numbers).
namespace coop::fishbody {

// The member has a bone map (Xiao, Goro, Ruby, Ungaga, Osmond: every member but Toan) and its model and Toan's fishing model read as the map expects.
// DC_COOP_MUTATE=fishbody_toan (twin): no member has one, every fisher is drawn as Toan, as before.
bool Mapped(int member);

// The member's right wrist's frame name in its own model (the rows hold the drawn body's to the pose).
const char *RightWrist(int member);

// fisher holds the fishing model (retail's _LOAD_MAIN_CHARA of c01d_turi, or the co-op's load into a walker's body).
bool HoldsFishingModel(dc::game::CCharacter *fisher);

// One frame's numbers, in the model's units (Toan stands 16.5 tall) and degrees; what the rows read.
struct Numbers {
    float hand[3];          // the member's right wrist, in the model's space
    float left[3];          // and its left wrist
    float hold[3];          // the right hand's holding point (the rod runs through it), in the model's space
    float tip[3];           // the rod's tip, in the model's space
    float hand_world[3];    // the right wrist in the world (what a draw's frames are held to)
    float palm, palm_toan;  // the right palm to the rod's line, theirs and Toan's
    float left_rod, left_rod_toan; // the left wrist to the rod's line
    float ankle_rise[2];    // each ankle's height against Toan's scaled (right, left)
    float ankle_slide[2];   // and its drift across the ground against Toan's scaled
    float floor;            // the lowest foot joint against where it rests (negative: into the ground)
    float bend[4], bend_toan[4]; // right elbow, left elbow, right knee, left knee
    float arm_scale, leg_scale;
};

// Where the rod is while the member holds it, in the world. It hangs on the drawn body's right hand: its line runs through the
// hand's holding point (the member's grip frame, in its palm) and it turns with that hand, as Toan's rod turns with his.
struct Rod {
    float root[3];     // the rod's own origin (its butt end)
    float turn[3][3];  // its turn, [row][column]
    float tip[3];      // its tip (where the fishing line starts)
    float tip_step[3]; // from Toan's rod's tip to this one (the float and the hook hang as far from it)
    float hold[3];     // the holding point in the hand
};

// Works the member's pose out from the fisher's (the fishing model's) pose now, and where the rod is in its hand (rod may be
// null). False when the member has no map or the model is not the fishing model. numbers may be null. A pure sum of the fishing
// model's frames and the two models' rest poses: every copy gets the same answer on the same tick, whether or not it draws the
// body. DC_COOP_MUTATE=fishbody_rod_toan (twin): the rod stays where Toan's hidden hand holds it.
bool Solve(dc::game::CCharacter *fisher, int member, Rod *rod, Numbers *numbers);

// The frame in the member's right hand that holds the rod ("~<frame>": the left hand's frame mirrored onto the right hand, as
// Ruby's, whose right hand has none), and how far up the rod's line from its origin the hold is.
const char *Grip(int member);

// The holding point in the member's right wrist's own frame (unit axes), whatever its grip is made of.
bool HoldInHand(int member, float *out);
float       HoldAlong();

// Every part a draw of chara draws now, by the engine's own rules (a frame with a mesh whose draw bits are on and no parent
// skipping it; every cloth the character wears), names apart by spaces; returns how many.
int DrawnParts(dc::game::CCharacter *chara, const char **names);

// The box round the joints of the body a fisher is drawn as (the member's solved pose, or Toan's own when the member has no
// map), in the world.
bool DrawnBox(dc::game::CCharacter *fisher, int member, float *low, float *high);

// Puts the pose of the last Solve of that member onto body (the member's own model, loaded by the caller) and bends its skin and
// its shadow's; PoseEnd gives the body's frames their own transforms back (its walk's). For a draw: Pose, draw, PoseEnd.
bool Pose(dc::game::CCharacter *body, int member);
void PoseEnd(dc::game::CCharacter *body);

// The fishing model's draw with none of Toan in it: his meshes, his cloth (the shawl and turban are cloth, drawn after the
// frames) and his shadow left out, the rod put in the member's hand (rod), the float and hook moved as far as its tip, until
// PropsOnlyEnd puts every byte back as it was. For a draw only: begin before the model's draw, end after it.
// DC_COOP_MUTATE=fishbody_shawl_left (twin): Toan's cloth stays in the draw.
void PropsOnlyBegin(dc::game::CCharacter *fisher, const Rod &rod);
void PropsOnlyEnd(dc::game::CCharacter *fisher);

} // namespace coop::fishbody
