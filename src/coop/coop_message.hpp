#pragma once

#include "coop/game_ns.hpp"

// coop-itemprompt
// A got-item, Atla or weapon message is its getter's (Jep's playtest, 2026-10-27: "P2's weapon-break and item-acquisition
// messages show on both screens ... the Atla acquired message sticks on P2's screen and never clears"). Both linked copies
// set the same message on the same tick (the system message, SetSystemMes, and the dungeon's own, DngMessMan); each
// message is tagged with the player it is for, and a copy draws it only when that is its own player. A key item's message
// (SharedItem: gate keys, the map and magic crystal, quest and key items; FloorKeyItem) is the party's and shows on both.
// Draw only: what the tick reads (the messages' counts, numbers and texts) is untouched, so the copies stay alike.
// Unlinked (solo, couch), or apart (the copy's world is its own player's alone), every message is drawn as before.
// DC_COOP_MUTATE=mesboth: every message on both copies again (the screens check's twin); meskeyown: a key item's message is
// its getter's alone (the shared check's twin); atlawait (coop_interact): her Atla's name waits for his Cross again.

namespace coop {

// Whose the next system message set is (1 player 1, 2 player 2), for as long as it lives: coop code that sets one for
// player 2 (her chest, Atla, pickup, trap) makes one round the call. Without one, a message is player 1's (the game's
// own, run for him), or player 2's while she drives a town event.
class MessageBy {
  public:
    explicit MessageBy(int player);
    ~MessageBy();

  private:
    int was_;
};

// sysmes.cpp: ItemGetMes names its item before it sets the message; SetSystemMes tags the message it set.
void SystemMesItem(int item);
void SystemMesSet(int message_no);
// SystemMesDraw: whether this copy draws the system message.
bool SystemMesShown();
// coop-closetitem: SystemMesStep: whether the player a system message waiting for a button is for pressed Cross this tick
// (game_pad: the game pad's Cross, the answer unlinked or apart).
bool SystemMesCross(bool game_pad);
// coop-closetglow: the player the system message up now is for (as SystemMesCross reads its Cross), or 0 unlinked, apart or none.
int SystemMesFor();

// coop-opt7: on a copy other than the opener's, a key item's got message from the opener's own presentation (which runs on the
// opener's copy alone): the party's, so shown here too, and it fades by itself as an Atla's name does for the others (Jep,
// 2026-10-27 and 10-31). Not a key item: nothing (the message is the opener's). Presentation only: this copy's message slot.
// DC_COOP_MUTATE=keymes_opener_only (twin): never shown on the others' screens.
void OthersKeyItemMes(int player, int item, int volume);

// Coop code that set DngMessMan's message for player 2 (her weapon's wear, break, build-up) says so after it.
void DngMesBy(int player);
// The dungeon's draw: whether this copy draws DngMessMan's message.
bool DngMesShown();
bool DngMesEnabledHere(); // coop-pt1109c: the game's DngMessMan.enabled as this copy's screen has it
// coop-p2hurt: the message the dungeon's draw shows (her own thirst's on her copy, in place of none or his), and her thirst's
// messages stepped each tick from her member's water (StepThirst), as CDngMessageMan::SetStatus_Dry steps his.
int  DngMesDrawn();
void PartnerDryStep(int chara, float water_max, float water_now);
int  PartnerDryMessage();

// coop_interact's PickUp: player 2 raised a pickup event for item (an attachment or story item), which the game's tick shows
// as the player's own (BtGetAttach_Init): its message is hers.
void PartnerPickupRaised(int item);

} // namespace coop
