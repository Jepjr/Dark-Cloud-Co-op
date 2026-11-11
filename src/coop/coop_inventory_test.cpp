// The co-op inventory test bot (DC_COOP_INVTEST=pickup|chest|use|send|save|linked): an in-game player
// that steps one inventory feature through with timeouts and asserts on the game's own state, then
// prints "RESULT: PASS <test>" or "RESULT: FAIL <test>: why" and ends the run. Its mutation twins
// (DC_COOP_MUTATE=invpickup|invchest|invuse|invsend|invfull|invsave|invdesync) must make it fail.
// scenarios/inventory/run.ps1 runs them. The bot holds pad 2's buttons (InventoryTestPad) and player
// 1's sends (SendItem) and reads only game state, so it is the same in every copy. "linked"
// only seeds both bags alike and leaves the pads to the input script. Nothing here runs unless
// DC_COOP_INVTEST is set.

#include "coop/game_ns.hpp"
#include "coop/coop_inventory.hpp"

#include <libvu0.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#include "dun/gameloop.hpp"
#include "dungeonmap.hpp"
#include "framevu1.hpp"
#include "app/gameloop.hpp"
#include "itemdata.hpp"
#include "mathutil.hpp"
#include "memcard.hpp"
#include "menu_inventory.hpp"
#include "randomitem.hpp"
#include "save/save_slots.hpp"
#include "stealitem.hpp"
#include "userstatus.hpp"

#include "coop/coop.hpp"
#include "coop/coop_camera.hpp" // FloorIntro
#include "coop/coop_combat.hpp"
#include "coop/coop_core.hpp"
#include "coop/coop_party.hpp"
#include "coop/netplay.hpp"

namespace dc::game {
extern CFrameVu1 *CharaFrame;
extern s32        gameTask;
extern s32        itemNowSel;
} // namespace dc::game

namespace coop {

namespace {

constexpr int kRunTicks = 6000;

struct Bot {
    const char   *test = nullptr;
    bool          mode = false;
    bool          done = false;
    int           stop_in = 0;
    int           ticks = 0;
    int           step = 0;
    int           t = 0;
    std::uint16_t buttons = 0;

    int p1_count = 0;
    int p2_count = 0;
    int p1_money = 0;
    int p2_gilda = 0;
    int life = 0;
    int box = -1;
    std::uint32_t hash = 0;
};

Bot g_bot;

int P1Count(int item) {
    return GetNowItemNum((short) item, &UserStatus->item_pack);
}

int P2Count(int item) {
    return GetNowItemNum((short) item, &g_bag.pack);
}

void Next() {
    g_bot.step++;
    g_bot.t = 0;
    g_bot.buttons = 0;
}

void Finish(bool pass, const char *why) {
    std::printf("RESULT: %s %s%s%s\n", pass ? "PASS" : "FAIL", g_bot.test, pass ? "" : ": ", pass ? "" : why);
    std::fflush(stdout);
    g_bot.done = true;
    g_bot.stop_in = 30;
    g_bot.buttons = 0;
}

void Fail(const char *why) {
    Finish(false, why);
}

// Holds mask on ticks t0 to t0+3 of the step, so it is one press.
void TapAt(int t0, std::uint16_t mask, std::uint16_t &into) {
    if (g_bot.t >= t0 && g_bot.t < t0 + 4) {
        into |= mask;
    } else if (g_bot.t >= t0 + 4 && g_bot.t < t0 + 6) {
        into &= ~mask;
    }
}

// Empties player 2's bag (a new game's), then puts count of item into it.
void SeedBag(int item, int count) {
    InventoryNewGame();
    (void) BagCount(ITEM_STONE); // seeds it

    for (int i = 0; i < count; i++) {
        for (int s = 0; s < g_bag.pack.num; s++) {
            if (g_bag.pack.item[s] < ITEM_DUNGEON_START) {
                g_bag.pack.item[s] = (s16) item;
                g_bag.pack.item_vol[s] = (s16) ItemDataToHaveCopy(item);
                break;
            }
        }
    }
}

void AddToBag(ITEM_PACK &pack, int item, int count) {
    for (int s = 0; s < pack.num && count > 0; s++) {
        if (pack.item[s] < ITEM_DUNGEON_START) {
            pack.item[s] = (s16) item;
            pack.item_vol[s] = (s16) ItemDataToHaveCopy(item);
            count--;
        }
    }
}

// Puts every copy of item in player 2's bag into its quick-use slot slot (1 to 3), which it selects.
void SlotBag(int slot, int item) {
    g_bag.selected = slot;
    PartnerSlotAssign(item);
}

// Fills every free slot of a pack with item, as far as CheckItemGet counts it full.
void FillPack(ITEM_PACK &pack, int item) {
    int carried = 0;

    for (int s = 0; s < pack.num; s++) {
        carried += pack.item[s] >= ITEM_DUNGEON_START ? 1 : 0;
    }

    for (int k = 0; k < 3; k++) {
        carried += pack.quick_item_slot[k] != -1 ? pack.quick_item_qty[k] : 0;
    }

    AddToBag(pack, item, pack.num - carried);
}

// Takes every copy of item out of player 1's pack slots (the quick-use slots stay).
void ClearP1(int item) {
    for (int s = 0; s < 103; s++) {
        if (UserStatus->item_pack.item[s] == item) {
            UserStatus->item_pack.item[s] = -1;
            UserStatus->item_pack.item_vol[s] = 0;
        }
    }
}

// Player 1's quick-use slot 1 holds count of item, and the D-pad has picked it.
void QuickSlot(int item, int count) {
    UserStatus->item_pack.quick_item_slot[0] = (s16) item;
    UserStatus->item_pack.quick_item_qty[0] = (s16) count;
    UserStatus->active_item_vol[0] = ItemDataToHaveCopy(item);
    itemNowSel = 1;
}

void Note(const char *what) {
    std::printf("coop-invtest: %s: P1 bread %d money %d, P2 bread %d stones %d gilda %d (%d slots), life %d/%d\n", what,
                P1Count(ITEM_BREAD), UserStatus->money, P2Count(ITEM_BREAD), P2Count(ITEM_STONE), g_bag.gilda, g_bag.pack.num, Life(),
                MaxLife());
}

// --- pickup: 30 gold and a bread fall at the partner's feet, out of the player's reach; both go to
// player 2 (its gilda, its bag), and player 1's pack and money stay as they were.
void Pickup() {
    switch (g_bot.step) {
        case 0:
            // In play first: the floor's arrival (coop-arrivalfull: it plays out on the floor) moves both players from where
            // the floor put them, so finds dropped at her feet before it lay ~1800 away from her afterwards.
            if (gameTask != GAME_TASK_PLAY || FloorIntro()) {
                g_bot.t = 0;
            }

            if (g_bot.t >= 60 && RandomItem != nullptr) {
                SeedBag(ITEM_STONE, 0);
                g_bot.p1_count = P1Count(ITEM_BREAD);
                g_bot.p1_money = UserStatus->money;
                g_bot.p2_count = P2Count(ITEM_BREAD);
                g_bot.p2_gilda = g_bag.gilda;

                float distance = DistVector(g_partner.pos, CharaFrame->position);

                if (distance < 8.0f) {
                    Fail("setup: the player stands within reach of the partner's feet");
                    return;
                }

                sceVu0FVECTOR at;
                sceVu0CopyVector(at, g_partner.pos);
                RandomItem->Set(at, 100, 30, -1);
                at[2] += 2.0f;
                RandomItem->Set(at, 101, -1, ITEM_BREAD);
                std::printf("coop-invtest: dropped 30 gold and a bread at the partner's feet, player %.0f away\n", distance);
                Note("before");
                Next();
            }
            break;
        case 1:
            if (P2Count(ITEM_BREAD) == g_bot.p2_count + 1 && g_bag.gilda == g_bot.p2_gilda + 30) {
                Note("after");

                if (P1Count(ITEM_BREAD) != g_bot.p1_count || UserStatus->money != g_bot.p1_money) {
                    Fail("player 1's pack or money changed");
                    return;
                }

                Finish(true, "");
            } else if (g_bot.t > 400) {
                Note("after");
                Fail(P1Count(ITEM_BREAD) > g_bot.p1_count || UserStatus->money > g_bot.p1_money
                         ? "the bread or gold went to player 1, not player 2's bag and purse"
                         : "player 2 never got the bread and the gold");
            }
            break;
    }
}

// --- chest: the partner stands at the nearest small chest, which holds a bread, and Cross opens it;
// the bread goes into player 2's bag, player 1's pack stays as it was.
void Chest() {
    switch (g_bot.step) {
        case 0:
            if (g_bot.t >= 60) {
                float best = 1.0e9f;
                int   chosen = -1;

                for (int i = 0; i < 48; i++) {
                    if (NowDngMap->events[i].kind != DNG_EVENT_TREASURE_BOX) {
                        continue;
                    }

                    const TREASURE_BOX &box = NowDngMap->boxes[NowDngMap->events[i].index];
                    float               distance = DistVector(NowDngMap->events[i].pos, g_partner.pos);

                    if (box.kind == TREASURE_BOX_SMALL && box.closed != 0 && distance < best) {
                        best = distance;
                        chosen = i;
                    }
                }

                if (chosen == -1) {
                    Fail("setup: no closed small chest on the floor");
                    return;
                }

                g_bot.box = NowDngMap->events[chosen].index;
                NowDngMap->boxes[g_bot.box].item_no = ITEM_BREAD;
                SeedBag(ITEM_STONE, 0);

                for (int axis : {0, 1, 2}) {
                    g_partner.pos[axis] = NowDngMap->events[chosen].pos[axis];
                }

                g_partner.chara->frame->SetPosition(g_partner.pos);
                g_bot.p1_count = P1Count(ITEM_BREAD);
                g_bot.p2_count = P2Count(ITEM_BREAD);
                std::printf("coop-invtest: partner at small chest %d (event %d), holding a bread\n", g_bot.box, chosen);
                Note("before");
                Next();
            }
            break;
        case 1:
            if (g_bot.t >= 20) {
                TapAt(20 + ((g_bot.t - 20) / 40) * 40, kInputCross, g_bot.buttons);
            }

            if (NowDngMap->boxes[g_bot.box].closed == 0 && g_bot.t > 30) {
                Note("after");

                if (P2Count(ITEM_BREAD) != g_bot.p2_count + 1) {
                    Fail(P1Count(ITEM_BREAD) > g_bot.p1_count ? "the chest's bread went to player 1's pack" : "the chest opened but no bread came");
                    return;
                }

                if (P1Count(ITEM_BREAD) != g_bot.p1_count) {
                    Fail("player 1's pack changed");
                    return;
                }

                Finish(true, "");
            } else if (g_bot.t > 400) {
                Fail("the partner's Cross never opened the chest");
            }
            break;
    }
}

// --- use: player 2's bag holds two bread and player 1's pack holds its own; Square eats one of
// player 2's and heals the partner, and player 1's bread stays.
void Use() {
    switch (g_bot.step) {
        case 0:
            if (g_bot.t >= 60 && !g_partner.attacking) {
                if (P1Count(ITEM_BREAD) <= 0) {
                    Fail("setup: player 1's pack holds no bread (--give 148)");
                    return;
                }

                SeedBag(ITEM_BREAD, 2);
                SlotBag(1, ITEM_BREAD);
                SetLife(std::max(1, MaxLife() - 30));
                g_bot.p1_count = P1Count(ITEM_BREAD);
                g_bot.p2_count = P2Count(ITEM_BREAD);
                g_bot.life = Life();
                Note("before");
                Next();
            }
            break;
        case 1:
            TapAt(10, kInputSquare, g_bot.buttons);

            if (g_bot.t == 40) {
                Note("after");

                if (Life() <= g_bot.life) {
                    Fail("the partner did not eat");
                } else if (P2Count(ITEM_BREAD) != g_bot.p2_count - 1) {
                    Fail("player 2's bag still holds both bread");
                } else if (P1Count(ITEM_BREAD) != g_bot.p1_count) {
                    Fail("the bread came out of player 1's pack");
                } else {
                    Finish(true, "");
                }
            }
            break;
    }
}

// --- send: player 1 sends a bread to player 2 (SendItem, as its bag screen's Select does); player 2's panel (Triangle,
// Right to Items, Square) sends one back; a full bag refuses either way, with nothing taken.
void SendTest() {
    char why[160];

    switch (g_bot.step) {
        case 0:
            // In free play first (coop-keytab, red on main 879dda87): the floor's opening scene (script 100, BtEventMode on)
            // holds her walk, and with it her pad panel, so a send from it before the scene ends did nothing ("panel page 0").
            // DC_COOP_MUTATE=invsend_scene (twin): the bot starts inside the scene, as before.
            if ((gameTask != GAME_TASK_PLAY || FloorIntro() || BtEventMode != 0) && !Mutated("invsend_scene")) {
                g_bot.t = 0;
            }

            if (g_bot.t >= 60 && !g_partner.attacking) {
                ClearP1(ITEM_BREAD);
                QuickSlot(ITEM_BREAD, 2);
                SeedBag(ITEM_STONE, 0);
                Note("start");
                Next();
            }
            break;
        case 1: // P1 -> P2
            if (g_bot.t == 5) {
                SendItem(1, ITEM_BREAD); // what the bag screen's Select calls (BagScreenSend; i-bag-screen drives the menu)
            }

            if (g_bot.t == 30) {
                Note("P1 sent");

                if (P1Count(ITEM_BREAD) != 1 || P2Count(ITEM_BREAD) != 1) {
                    std::snprintf(why, sizeof why, "P1->P2: P1 bread %d (want 1), P2 bread %d (want 1)", P1Count(ITEM_BREAD), P2Count(ITEM_BREAD));
                    Fail(why);
                    return;
                }

                Next();
            }
            break;
        case 2: // P2 -> P1 through the panel
            TapAt(5, kInputTriangle, g_bot.buttons);
            TapAt(20, kInputRight, g_bot.buttons);
            TapAt(35, kInputSquare, g_bot.buttons);
            TapAt(55, kInputCircle, g_bot.buttons);

            if (g_bot.t == 50) {
                Note("P2 sent");

                if (P1Count(ITEM_BREAD) != 2 || P2Count(ITEM_BREAD) != 0) {
                    std::snprintf(why, sizeof why, "P2->P1: P1 bread %d (want 2), P2 bread %d (want 0), panel page %d", P1Count(ITEM_BREAD),
                                  P2Count(ITEM_BREAD), g_combat.page);
                    Fail(why);
                    return;
                }
            }

            if (g_bot.t == 80) {
                Next();
            }
            break;
        case 3: // P2 -> P1 with the chord: Select held, Square
            if (g_bot.t == 1) {
                SeedBag(ITEM_BREAD, 1);
                SlotBag(1, ITEM_BREAD);
                g_bot.p1_count = P1Count(ITEM_BREAD);
            }

            if (g_bot.t >= 5 && g_bot.t < 30) {
                g_bot.buttons |= kInputSelect;
            } else {
                g_bot.buttons &= ~kInputSelect;
            }

            TapAt(12, kInputSquare, g_bot.buttons);

            if (g_bot.t == 40) {
                Note("P2 chord sent");

                if (P2Count(ITEM_BREAD) != 0 || P1Count(ITEM_BREAD) != g_bot.p1_count + 1) {
                    std::snprintf(why, sizeof why, "P2 chord: P2 bread %d (want 0), P1 bread %d (want %d)", P2Count(ITEM_BREAD),
                                  P1Count(ITEM_BREAD), g_bot.p1_count + 1);
                    Fail(why);
                    return;
                }

                // Back to two in player 1's pack for the full-bag steps.
                ClearP1(ITEM_BREAD);
                AddToBag(UserStatus->item_pack, ITEM_BREAD, 1);
                Next();
            }
            break;
        case 4: // P2's bag full: P1's send is refused and P1 keeps its bread
            if (g_bot.t == 1) {
                SeedBag(ITEM_STONE, 0);
                FillPack(g_bag.pack, ITEM_STONE);
                g_bot.p2_count = P2Count(ITEM_STONE);
                Note("P2 full");
            }

            if (g_bot.t == 5) {
                SendItem(1, ITEM_BREAD); // what the bag screen's Select calls (BagScreenSend; i-bag-screen drives the menu)
            }

            if (g_bot.t == 30) {
                Note("P1 sent to a full bag");

                if (P1Count(ITEM_BREAD) != 2 || P2Count(ITEM_BREAD) != 0 || P2Count(ITEM_STONE) != g_bot.p2_count) {
                    std::snprintf(why, sizeof why, "full P2 bag: P1 bread %d (want 2), P2 bread %d (want 0), stones %d (want %d)",
                                  P1Count(ITEM_BREAD), P2Count(ITEM_BREAD), P2Count(ITEM_STONE), g_bot.p2_count);
                    Fail(why);
                    return;
                }

                Next();
            }
            break;
        case 5: // P1's pack full: P2's send is refused and P2 keeps its bread
            if (g_bot.t == 1) {
                SeedBag(ITEM_BREAD, 1);
                FillPack(UserStatus->item_pack, ITEM_STONE);
                g_bot.p1_count = P1Count(ITEM_BREAD);
                Note("P1 full");
            }

            TapAt(5, kInputTriangle, g_bot.buttons);
            TapAt(20, kInputSquare, g_bot.buttons);
            TapAt(40, kInputCircle, g_bot.buttons);

            if (g_bot.t == 35) {
                Note("P2 sent to a full pack");

                if (P2Count(ITEM_BREAD) != 1 || P1Count(ITEM_BREAD) != g_bot.p1_count) {
                    std::snprintf(why, sizeof why, "full P1 pack: P2 bread %d (want 1), P1 bread %d (want %d), panel page %d", P2Count(ITEM_BREAD),
                                  P1Count(ITEM_BREAD), g_bot.p1_count, g_combat.page);
                    Fail(why);
                    return;
                }

                Finish(true, "");
            }
            break;
    }
}

// --- save: player 2's bag (three bread, a bomb, 77 gilda) is written beside the autosave, emptied,
// and comes back when the autosave loads; the game's own save file stays as it was made.
void Save() {
    switch (g_bot.step) {
        case 0:
            if (g_bot.t >= 60) {
                SeedBag(ITEM_BREAD, 3);
                AddToBag(g_bag.pack, ITEM_BOMB, 1);
                g_bag.gilda = 77;
                g_bot.hash = InventoryHash();
                g_bot.p1_count = P1Count(ITEM_BREAD);

                std::error_code error;
                std::filesystem::remove(dc::save::SaveSlotDirectory(dc::save::kSaveSlotAutosave) / "coop-p2.dat", error);

                if (!SaveSlotWriteAutosave()) {
                    Fail("the autosave was not written");
                    return;
                }

                bool sidecar = std::filesystem::exists(dc::save::SaveSlotDirectory(dc::save::kSaveSlotAutosave) / "coop-p2.dat");
                bool image = std::filesystem::exists(dc::save::SaveSlotPath(dc::save::kSaveSlotAutosave));

                std::printf("coop-invtest: autosave %s (%s), sidecar %s\n", dc::save::SaveSlotPath(dc::save::kSaveSlotAutosave).string().c_str(),
                            image ? "written" : "missing", sidecar ? "written" : "missing");
                InventoryNewGame();
                Note("emptied");

                if (P2Count(ITEM_BREAD) != 0 || g_bag.gilda != 0) {
                    Fail("setup: the bag did not empty");
                    return;
                }

                Next();
            }
            break;
        case 1:
            if (g_bot.t == 10) {
                // As the save menu does before it loads (a --jump run never opened it).
                McAccess.InitForMC();
                int loaded = McAccess.LoadFromMc(dc::save::kSaveSlotAutosave);

                Note("loaded");

                if (loaded != 1) {
                    Fail("the autosave did not load");
                } else if (P2Count(ITEM_BREAD) != 3 || P2Count(ITEM_BOMB) != 1 || g_bag.gilda != 77 || InventoryHash() != g_bot.hash) {
                    Fail("player 2's bag did not come back with the save");
                } else if (P1Count(ITEM_BREAD) != g_bot.p1_count) {
                    Fail("player 1's pack changed across the save");
                } else {
                    Finish(true, "");
                }
            }
            break;
    }
}

// --- slots (linked): player 1's quick-use slots hold two bread (slot 1 picked), player 2's three stones
// and a bread (slot 1 picked); pad 2's Right picks player 2's slot 2. Both copies must agree on that
// (it is simulation), and what the top of the screen draws must be player 2's slots and selection on
// player 2's copy, and player 1's on player 1's.
void Slots() {
    char why[200];

    switch (g_bot.step) {
        case 0:
            if (g_bot.t >= 60 && !g_partner.attacking) {
                if (NetplayLocalPlayer() == 0) {
                    Fail("mode: not linked");
                    return;
                }

                ClearP1(ITEM_BREAD);
                QuickSlot(ITEM_BREAD, 2);
                UserStatus->item_pack.quick_item_slot[1] = -1;
                UserStatus->item_pack.quick_item_slot[2] = -1;
                SeedBag(ITEM_STONE, 3);
                AddToBag(g_bag.pack, ITEM_BREAD, 1);
                SlotBag(2, ITEM_BREAD);
                SlotBag(1, ITEM_STONE);
                Note("seeded");
                Next();
            }
            break;
        case 1: {
            TapAt(5, kInputRight, g_bot.buttons);

            if (g_bot.t < 60) {
                break;
            }

            int items[3];
            int counts[3];
            int selected = 0;
            bool drawn = DrawnSlots(items, counts, &selected);
            int player = NetplayLocalPlayer();
            const ITEM_PACK &own = player == 2 ? g_bag.pack : UserStatus->item_pack;
            int own_selected = player == 2 ? g_bag.selected : itemNowSel;
            bool same = drawn && selected == own_selected;
            bool like_p1 = drawn && selected == itemNowSel;

            for (int k = 0; k < 3; k++) {
                same = same && items[k] == own.quick_item_slot[k] && (items[k] == -1 || counts[k] == own.quick_item_qty[k]);
                like_p1 = like_p1 && items[k] == UserStatus->item_pack.quick_item_slot[k];
            }

            std::printf("coop-invtest: drawn slots %d x%d, %d x%d, %d x%d, selected %d on player %d's copy; P1 %d %d %d sel %d; P2 %d %d %d sel %d\n",
                        items[0], counts[0], items[1], counts[1], items[2], counts[2], selected, player,
                        UserStatus->item_pack.quick_item_slot[0], UserStatus->item_pack.quick_item_slot[1],
                        UserStatus->item_pack.quick_item_slot[2], itemNowSel, g_bag.pack.quick_item_slot[0],
                        g_bag.pack.quick_item_slot[1], g_bag.pack.quick_item_slot[2], g_bag.selected);

            if (g_bag.selected != 2) {
                std::snprintf(why, sizeof why, "pad 2's Right did not pick P2's slot 2 (selected %d)", g_bag.selected);
                Fail(why);
            } else if (!same) {
                std::snprintf(why, sizeof why, "player %d's window drew %s slots, not its own player's", player, like_p1 ? "player 1's" : "other");
                Fail(why);
            } else if (player == 2 && like_p1) {
                Fail("player 2's window drew the same slots as player 1's");
            } else {
                Finish(true, "");
            }
            break;
        }
    }
}

// --- steal: player 2's hit steals a bread from a monster (the roll itself is the combat thread's; the
// bot starts the steal as a won roll does, through StealSet). The room check goes by player 2's bag,
// the bread flies to the partner and lands in player 2's bag; player 1's pack stays as it was.
void Steal() {
    switch (g_bot.step) {
        case 0:
            if (g_bot.t >= 60 && !g_partner.attacking) {
                int owner = g_partner.loaded_chara;

                SeedBag(ITEM_STONE, 0);
                FillPack(g_bag.pack, ITEM_STONE);

                int full = StealRoomCheck(owner, ITEM_BREAD);

                SeedBag(ITEM_STONE, 0);

                if (full != 1 || StealRoomCheck(owner, ITEM_BREAD) != 0) {
                    Fail("the steal's room check does not go by player 2's bag");
                    return;
                }

                g_bot.p1_count = P1Count(ITEM_BREAD);
                g_bot.p2_count = P2Count(ITEM_BREAD);

                float at[4] = {g_partner.pos[0] + 30.0f, g_partner.pos[1] + 12.0f, g_partner.pos[2] + 30.0f, 1.0f};

                StealSet(at, ITEM_BREAD, owner);
                Note("stolen");
                Next();
            }
            break;
        case 1: {
            bool flying = false;

            for (int i = 0; i < STEAL_ITEM_MAX; i++) {
                flying = flying || (StealItem.state[i] != StealItemState::None && StealItem.item[i] == ITEM_BREAD);
            }

            if (P2Count(ITEM_BREAD) == g_bot.p2_count + 1 && !flying) {
                Note("landed");
                if (P1Count(ITEM_BREAD) != g_bot.p1_count) {
                    Fail("player 1's pack changed");
                } else {
                    Finish(true, "");
                }
            } else if (!flying && g_bot.t > 30) {
                Note("landed");
                Fail(P1Count(ITEM_BREAD) > g_bot.p1_count ? "player 2's stolen bread went to player 1" : "the stolen bread landed nowhere");
            } else if (g_bot.t > 900) {
                Fail("the stolen bread never landed");
            }
            break;
        }
    }
}

// --- linked: both bags seeded alike (player 1's quick-use slot six bread, player 2 four bread, two
// stones and 20 gilda); the input script then sends both ways. A line every 600 ticks for the run
// script to compare between the copies.
void Linked() {
    if (g_bot.step == 0) {
        ClearP1(ITEM_BREAD);
        QuickSlot(ITEM_BREAD, 6);
        SeedBag(ITEM_BREAD, 4);
        AddToBag(g_bag.pack, ITEM_STONE, 2);
        SlotBag(2, ITEM_STONE);
        SlotBag(1, ITEM_BREAD);
        g_bag.gilda = 20;
        Note("seeded");
        Next();
        return;
    }

    if (g_bot.ticks % 600 == 0) {
        std::printf("coop-invtest: linked tick %d bag %08x P1 bread %d P2 bread %d stones %d\n", g_bot.ticks, InventoryHash(),
                    P1Count(ITEM_BREAD), P2Count(ITEM_BREAD), P2Count(ITEM_STONE));
    }
}

} // namespace

void InventoryTestTick() {
    static const char *test = TestEnv("DC_COOP_INVTEST");

    if (test == nullptr) {
        return;
    }

    g_bot.test = test;

    if (g_bot.done) {
        if (g_bot.stop_in > 0 && --g_bot.stop_in == 0) {
            dc::app::GameRequestStop();
        }

        return;
    }

    // The mode first: a dungeon floor, co-op on with a bag each, the partner on it.
    if (!g_bot.mode) {
        // (coop-fx51d: not before a state file the copy is to load is put back: a bot started on the floor before the
        // fixture's load at tick 60 had its seeding and its finds undone by it, i-own-bags -Fixture)
        if (!Enabled() || NowDngMap == nullptr || CharaFrame == nullptr || !g_partner.visible || g_partner.loaded_chara < 0 ||
            NetplayStateLoadPending()) {
            return;
        }

        g_bot.mode = true;
        std::printf("coop-invtest: mode dungeon %d floor %d, task %d, P2 chara %d, own bags %d, link player %d, test %s, mutation %s\n",
                    (int) selectMapNo, (int) UserStatus->cur_floor, (int) gameTask, g_partner.loaded_chara, (int) OwnBags(),
                    NetplayLocalPlayer(), test, TestEnvOr("DC_COOP_MUTATE", "none"));

        if (!OwnBags()) {
            Fail("mode: game.coop_own_bags is off");
            return;
        }
    }

    if (++g_bot.ticks > kRunTicks && std::strcmp(test, "linked") != 0) {
        char why[64];

        std::snprintf(why, sizeof why, "timed out at step %d", g_bot.step);
        Fail(why);
        return;
    }

    g_bot.t++;
    g_partner.invincible = std::max(g_partner.invincible, 2);

    if (std::strcmp(test, "pickup") == 0) {
        Pickup();
    } else if (std::strcmp(test, "chest") == 0) {
        Chest();
    } else if (std::strcmp(test, "use") == 0) {
        Use();
    } else if (std::strcmp(test, "send") == 0) {
        SendTest();
    } else if (std::strcmp(test, "save") == 0) {
        Save();
    } else if (std::strcmp(test, "linked") == 0) {
        Linked();
    } else if (std::strcmp(test, "slots") == 0) {
        Slots();
    } else if (std::strcmp(test, "steal") == 0) {
        Steal();
    } else {
        Fail("unknown test");
    }
}

bool InventoryTestPad(InputPadState &pad) {
    if (g_bot.test == nullptr || !g_bot.mode || g_bot.done || std::strcmp(g_bot.test, "linked") == 0) {
        return false;
    }

    pad.buttons = g_bot.buttons;
    return true;
}

} // namespace coop
