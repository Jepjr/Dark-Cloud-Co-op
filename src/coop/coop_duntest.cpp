// coop-dungeonsuite: the dungeon interaction suite's driver (qa\tests\x-dungeon-suite.ps1). A test hook only: nothing here runs
// without DC_COOP_DUNTEST, set alike on both copies, and it runs from the link's tick (NetplayTestTick; NetplayTestFloorApart
// apart), so linked copies do the same on the same tick.
//
// DC_COOP_DUNTEST=<object>: at press-clock tick DC_COOP_DUNTEST_AT (DC_COOP_MENUPRESS's clock, default 20) the player
// DC_COOP_DUNTEST_WHO (1 or 2, default 2) is put where its Cross or Square uses the object; the test's own presses
// (DC_COOP_MENUPRESS) then use it, so the game's input paths do the rest. Objects:
//   small, large, trapped, mimic, map, crystal  the closed chest nearest the player (a large one for large and trapped,
//        trapped: trap DC_COOP_DUNTEST_TRAP, default 1; mimic: the chest made a mimic of the first monster still asleep;
//        map, crystal: a small chest whose item becomes the map or the magic crystal), the user on its event point;
//        coop-fixload: DC_COOP_DUNTEST_ITEM=N puts item N in a small, large or trapped chest (else the floor's own: on a
//        fixed fixture floor the nearest small chest held a key item, whose message is both players', every run)
//   atla      the Atla ball nearest the player still on the floor
//   gold      50 gold at the user's feet;  item  a Bread at the user's feet;  gatekey  the floor's gate key at the user's feet
//   lock      the floor's gate key in the party's pack, the user on the gate's lock (gatekey_N)
//   gate      the gate's lock opened (as its key does), the user on the gate's way out (out_N)
//   trap      a trap circle of kind DC_COOP_DUNTEST_TRAP (default 6, money down) under the user
//   spring    the user in the floor's healing zone, both players' water at 10
//   break     coop-itemprompt: the user's weapon worn to nothing and worn once more, so it breaks (the user's own wear path:
//             retail's BattleSubWeaponDmg for player 1, coop_combat's PartnerWeaponWear for player 2)
//   event:<prefix>  the user on the first event whose name starts with prefix
// DC_COOP_DUNTEST_BOTH=1: the other player is put on the same spot one tick later (two at one chest).
// DC_COOP_DUNTEST_AGAIN=N (coop-actorswap): at press clock N the same player is put at the same kind of object again, the
// nearest still unused (a second small chest after the first), "duntest: again pN at <object>: placed".
// coop-chestfix: DC_COOP_DUNTEST_POISON=1: the user is poisoned as it is placed (a bat's bite), so its poison's bites (every 180
// ticks) fall inside a presentation that lasts longer (Jep's rule: the opener takes no damage until control comes back).
// coop-fixplace: for an event slot (lock, gate, lockmsg, event:) the user goes where the floor holds them and the game's own
// searches find that slot, worked out from the event and the floor's polygons: its point, then 8 ways at 0.5 and 0.85 of its
// reach, the first with ground within 8 of the point's height (GatherPolys, GetFootPoly: the dungeon walk's own) from which
// SearchDataSlotPos (player 1's Square, the gate's Cross: 3D, the reach) and SearchDataSlotPos2 (her Cross: 2D, 40 up or
// down) both find the slot ("duntest: pN at event ...: spot ..."), and player 1's camera is snapped onto him there: his walk
// collides with the parts of the cells DrawMap lays out near the camera, so put far from where the camera was he stood on
// nothing and fell through until it caught up (y -66 by clock 38 on a fixture placed at clock 1: train 29, seeds 1, 3, 4, 5;
// the long path, placed at clock 20, fell too but the Square's 40 of height still found the lock). None: "...: no spot ..."
// (the test fails). 12 ticks on, once: "...: kept" or "...: FAIL ..." (the user fell, or the game no longer finds the slot).
// DC_COOP_MUTATE=placeraw: the point, as before: no camera snap, nothing checked.
// DC_COOP_DUNTEST_ALSO=<object>: coop-chestbeside: the other player is put at an object of its own one tick later (an Atla for
// him while she is at a chest), its own presses then using it.
// Lines, all on stderr: once per floor "duntest: floor ..." (what the floor holds: the suite's catalogue) and
// "duntest: events: ..."; "duntest: put pN on <object> ..." at the placement; then each link tick on a floor at which any of
// its values changes (and every 120 ticks) "duntest: tick L clock C task K ...", the values the test compares.

#include "coop/game_ns.hpp"
#include "coop/coop.hpp"
#include "coop/netplay.hpp"

#include <SDL3/SDL.h>
#include <libvu0.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "btactstatus.hpp"
#include "btitem.hpp"
#include "btmisc.hpp"
#include "camerafollow.hpp" // coop-fixplace: the camera snapped onto player 1
#include "character.hpp"
#include "collision.hpp" // coop-fixplace: GetFootPoly
#include "dngstatusdata.hpp"
#include "dun/gameloop.hpp"
#include "app/gameloop.hpp"
#include "dungeoneventdata.hpp"
#include "dungeoneventman.hpp"
#include "dungeonmap.hpp"
#include "frame.hpp"
#include "itemdata.hpp"
#include "dataread.hpp"      // coop-chestworld: read_buffer
#include "mainitemmodel.hpp" // coop-chestworld: activeItem
#include "main.hpp"
#include "mathutil.hpp"
#include "monstorunit.hpp"
#include "randomitem.hpp"
#include "userstatus.hpp"
#include "dngmessageman.hpp"
#include "menu_save.hpp" // BattleSubWeaponDmg
#include "sysmes.hpp"

#include "coop/coop_camera.hpp" // coop-fx51d: FloorIntro
#include "coop/coop_combat.hpp" // coop-itemprompt: PartnerWeaponWear
#include "coop/coop_core.hpp"
#include "coop/coop_duntest.hpp"
#include "coop/coop_fullmenu.hpp" // coop-chestworld: FullMenuOpenFor
#include "coop/coop_interact.hpp"
#include "coop/coop_inventory.hpp"
#include "coop/coop_prompt.hpp" // coop-atlaswap: PromptTradeForTest
#include "coop/coop_worlds.hpp"

namespace dc::game {
extern s32 gameTask;
extern s32 mode;
extern s32 itemNowSel;                             // coop-chestworld: the quick slot in hand (gameloop.cpp)
void       BtSetActiveItemModel(unsigned int *pack); // coop-chestworld: the quick slots' models (btitem.cpp)

void PortRandSetState(unsigned int state); // port/src/runtime.cpp
extern int SystemMesNo; // sysmes.cpp
} // namespace dc::game

namespace coop {

namespace {

struct DunTest {
    std::string object;
    int         who = 2;
    int         at = 20;
    bool        both = false;
    int         floor = -1;  // map * 256 + floor of the last floor line
    bool        put = false; // the user placed, on this floor
    int         box = -1;    // the chosen chest (its boxes[] index) or Atla ball
    int         event = -1;  // its map event (events[]) or event slot (NowEventMan)
    int         item = -1;   // the item the test counts in each bag
    int         second = -1; // the press clock's tick at which the other player goes to the same spot (both)
    int         other_at = -1; // the press clock's tick at which the other player goes to its own object (DC_COOP_DUNTEST_ALSO)
    float       spot[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    std::string last;
    std::int64_t last_tick = 0;
    std::string object_now;        // the object's own part of the state (State)
    std::string object_first;      // ... at the first line after the placement
    int          placed_floor = -1; // the floor of the placement
    std::int64_t decided = -1;      // the link tick its state first changed (DC_COOP_DUNTEST_QUIT)
    // coop-fixplace: the event slot the user has to be at (lock, gate, event:), the spot taken, and the press clock's tick of
    // the one check that the user is still there (Kept)
    CDungeonEventData *target = nullptr;
    float              target_at[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    std::int64_t       check = -1;
    // coop-atlaswap: DC_COOP_DUNTEST_SWAP: the members the swap gives the player and the partner, and whether it has been made
    int  swap_player = -1;
    int  swap_partner = -1;
    bool swapped = false;
};

DunTest g_test;

float *UserPos(int who) {
    return who == 1 ? CharaMain.pos : g_partner.pos;
}

void PutAt(int who, const float *at) {
    if (who == 1) {
        float to[4] = {at[0], at[1], at[2], 1.0f};

        CharaMain.SetPosition(to);
        sceVu0CopyVector(CharaMain.pos, to);
    } else if (g_partner.chara != nullptr) {
        for (int axis : {0, 1, 2}) {
            g_partner.pos[axis] = at[axis];
        }

        g_partner.chara->frame->SetPosition(g_partner.pos);
    }
}

int Count(const ITEM_PACK &pack, int item) {
    int n = 0;

    for (int i = 0; item >= 0 && i < 103; i++) {
        n += pack.item[i] == item ? 1 : 0;
    }

    return n;
}

CDungeonEventData *SlotNamed(const char *prefix) {
    for (CDungeonEventData &data : NowEventMan->event) {
        if (data.event != nullptr && data.enabled && std::strncmp(data.event->name, prefix, std::strlen(prefix)) == 0) {
            return &data;
        }
    }

    return nullptr;
}

// The gate's way out with the lock's number (out_N for gatekey_N), as UseGateKey finds it.
CDungeonEventData *WayOut(const CDungeonEventData &lock) {
    const char *suffix = std::strchr(lock.event->name, '_');

    for (CDungeonEventData &data : NowEventMan->event) {
        const char *own = data.event != nullptr ? std::strchr(data.event->name, '_') : nullptr;

        if (suffix != nullptr && own != nullptr && data.enabled && std::strncmp(data.event->name, "out", 3) == 0 && std::strcmp(own, suffix) == 0) {
            return &data;
        }
    }

    return SlotNamed("out");
}

// What the floor holds, once per floor: the catalogue's census.
void FloorLines() {
    int small = 0, large = 0, trapped = 0, mimics = 0, atla = 0, heal = 0, water = 0, circles = 0;

    for (const DUNGEON_EVENT &event : NowDngMap->events) {
        if (event.kind == DNG_EVENT_TREASURE_BOX) {
            const TREASURE_BOX &box = NowDngMap->boxes[event.index];

            (box.kind == TREASURE_BOX_LARGE ? large : small)++;
            trapped += box.trap_no != 0 ? 1 : 0;
        } else if (event.kind == DNG_EVENT_MIMIC) {
            mimics++;
        } else if (event.kind == DNG_EVENT_ATRA && NowDngMap->atra[event.index].used != 0) {
            atla++;
        }
    }

    for (int i = 0; i < 400; i++) {
        int parts_no = NowDngMap->cells[i].parts_no;

        if (parts_no != MAP_PARTS_NONE) {
            heal += NowDngMap->parts[parts_no].heal_on != 0 ? 1 : 0;
            water += NowDngMap->parts[parts_no].water.used != 0 ? 1 : 0;
        }
    }

    for (const MAP_TRAP_CIRCLE &circle : NowDngMap->trap_circle) {
        circles += circle.state != 0 ? 1 : 0;
    }

    std::fprintf(stderr, "duntest: floor map %d floor %d (player %d%s): small chests %d, large %d, trapped %d, mimics %d, atla %d, heal parts %d, "
                         "water parts %d, trap circles %d, gate key %d\n",
                 selectMapNo, (int) UserStatus->cur_floor, NetplayLocalPlayer(), CoopWorldsApart() ? ", apart" : "", small, large, trapped, mimics, atla,
                 heal, water, circles, NowDngMap->gate_key_no);

    std::string list;

    for (const CDungeonEventData &data : NowEventMan->event) {
        if (data.event != nullptr && data.enabled) {
            char one[96];

            std::snprintf(one, sizeof one, " %s(script %d hold %d switch %d chara %d)", data.event->name, data.event->script_no, data.hold,
                          data.switch_on, data.event->chara_no);
            list += one;
        }
    }

    std::fprintf(stderr, "duntest: events:%s\n", list.c_str());
}

int NearestChest(bool large) {
    float best = 1.0e9f;
    int   chosen = -1;

    for (int i = 0; i < 48; i++) {
        const DUNGEON_EVENT &event = NowDngMap->events[i];

        if (event.kind != DNG_EVENT_TREASURE_BOX || NowDngMap->boxes[event.index].closed == 0 ||
            (NowDngMap->boxes[event.index].kind == TREASURE_BOX_LARGE) != large) {
            continue;
        }

        float distance = DistVector((float *) event.pos, CharaMain.pos);

        if (distance < best) {
            best = distance;
            chosen = i;
        }
    }

    return chosen;
}

// Puts the user at the object (and sets the object up). False when the floor has none: the line says so.
// Whether the game's searches find the target slot, and only it, from at: player 1's Square and the gate's Cross ask
// SearchDataSlotPos (3D, the slot's reach), her Cross SearchDataSlotPos2 (2D, 40 up or down, reach + 10).
bool Finds(int who, const float *from) {
    float at[4] = {from[0], from[1], from[2], 1.0f};

    return NowEventMan->SearchDataSlotPos2(at) == g_test.target && (who != 1 || NowEventMan->SearchDataSlotPos(at) == g_test.target);
}

// The floor under (x, z) within 8 of y, as the dungeon walk finds it.
bool GroundAt(float x, float y, float z, float *ground) {
    constexpr float kReach = 8.0f;
    float           at[4] = {x, y + kReach, z, 1.0f};
    CCPoly         *polys;
    const int       count = GatherPolys(at, &polys);
    CCPoly          poly;
    sceVu0FVECTOR   hit;

    if (count <= 0 || !GetFootPoly(at, 2.0f * kReach, &poly, hit, polys, count, 0)) {
        return false;
    }

    *ground = hit[1];
    return true;
}

// Puts the user on the slot: the first spot round its point with ground under it from which the searches find it.
void SlotSpot(CDungeonEventData *slot) {
    g_test.target = slot;
    sceVu0CopyVector(g_test.target_at, slot->pos);
    g_test.check = -1;

    if (Mutated("placeraw")) {
        PutAt(g_test.who, slot->pos);
        return;
    }

    const float reach = slot->event->radius;

    for (int k = 0; k < 17; k++) {
        const float angle = (float) ((k - 1) % 8) * 0.785398f;
        const float far = k == 0 ? 0.0f : k <= 8 ? 0.5f : 0.85f;
        float       at[4] = {slot->pos[0] + std::cos(angle) * reach * far, slot->pos[1], slot->pos[2] + std::sin(angle) * reach * far, 1.0f};
        float       ground;

        if (!GroundAt(at[0], at[1], at[2], &ground)) {
            continue;
        }

        at[1] = ground;

        if (!Finds(g_test.who, at)) {
            continue;
        }

        PutAt(g_test.who, at);
        sceVu0CopyVector(g_test.spot, at);
        g_test.check = NetplayTestPressClock() + 12;

        if (g_test.who == 1 && NowCamera__3 != nullptr) {
            NowCamera__3->SetFollow(at[0], at[1], at[2]);
            NowCamera__3->Step(-1); // the eye onto its follow point at once (no steps between)
        }

        std::fprintf(stderr, "duntest: p%d at event %s: spot %d at (%.1f %.1f %.1f) on its floor, its point (%.1f %.1f %.1f) reach %.1f\n", g_test.who,
                     slot->event->name, k, at[0], at[1], at[2], slot->pos[0], slot->pos[1], slot->pos[2], reach);
        return;
    }

    PutAt(g_test.who, slot->pos);
    std::fprintf(stderr, "duntest: p%d at event %s: no spot with floor from which the game finds it, round (%.1f %.1f %.1f) reach %.1f\n", g_test.who,
                 slot->event->name, slot->pos[0], slot->pos[1], slot->pos[2], reach);
}

// 12 ticks after the put, once: the user still stands there and the searches still find the slot.
void Kept(std::int64_t clock) {
    if (g_test.target == nullptr || g_test.check < 0 || clock < g_test.check) {
        return;
    }

    g_test.check = -1;

    const float *user = UserPos(g_test.who);
    const bool   stands = user[1] > g_test.spot[1] - 2.0f;
    const bool   finds = Finds(g_test.who, user);

    std::fprintf(stderr, "duntest: p%d at event %s: %s at (%.1f %.1f %.1f)\n", g_test.who, g_test.target->event->name,
                 stands && finds ? "kept" : !stands ? "FAIL: the user left the spot (fell)" : "FAIL: the game no longer finds the slot", user[0], user[1],
                 user[2]);
}

bool Place() {
    const std::string &o = g_test.object;
    float             *user = UserPos(g_test.who);
    const char        *what = o.c_str();

    const bool big = o == "large" || o == "trapped" || o == "piero";

    if (o == "small" || big || o == "mimic" || o == "map" || o == "crystal") {
        int chosen = NearestChest(big);

        // Wise Owl Forest's first floor has no large chest: the nearest small one becomes one, with a weapon in it.
        if (chosen < 0 && big && (chosen = NearestChest(false)) >= 0) {
            TREASURE_BOX &made = NowDngMap->boxes[NowDngMap->events[chosen].index];

            made.kind = TREASURE_BOX_LARGE;
            made.item_no = ITEM_WEAPON_BONE_SLINGSHOT;
            std::fprintf(stderr, "duntest: no large chest: small chest event %d made large with item %d\n", chosen, made.item_no);
        }

        if (chosen < 0) {
            std::fprintf(stderr, "duntest: no closed %s chest on the floor\n", big ? "large" : "small");
            return false;
        }

        DUNGEON_EVENT &event = NowDngMap->events[chosen];
        TREASURE_BOX  &box = NowDngMap->boxes[event.index];

        if (o == "large") {
            box.trap_no = 0;
        } else if (o == "trapped") {
            box.trap_no = TestEnvInt("DC_COOP_DUNTEST_TRAP", 1);
        } else if (o == "piero") {
            box.trap_no = 5; // coop-p2prompt: the clown's chest (event script 16)
        } else if (o == "map") {
            box.item_no = ITEM_MAP;
        } else if (o == "crystal") {
            box.item_no = ITEM_MAP + 1; // the magic crystal (OpenBox: ITEM_MAP and the one after it)
        } else if (o == "mimic") {
            int monster = -1;

            for (int i = 0; i < 16 && monster < 0; i++) {
                monster = NowMonstorUnit->monster[i].state == 1 && NowMonstorUnit->monster[i].hp > 0 ? i : -1;
            }

            if (monster < 0) {
                std::fprintf(stderr, "duntest: no sleeping monster to be the mimic\n");
                return false;
            }

            event.kind = DNG_EVENT_MIMIC;
            box.item_no = monster;
        }

        if ((o == "small" || big) && TestEnvInt("DC_COOP_DUNTEST_ITEM", -1) >= 0) {
            box.item_no = TestEnvInt("DC_COOP_DUNTEST_ITEM", -1);
        }

        g_test.box = event.index;
        g_test.event = chosen;
        g_test.item = o == "mimic" || o == "map" || o == "crystal" ? -1 : box.item_no;
        sceVu0CopyVector(g_test.spot, event.pos);
        PutAt(g_test.who, event.pos);
        std::fprintf(stderr, "duntest: put p%d on %s chest event %d (box %d, kind %d, trap %d, item %d, reach %.0f), other %.0f away\n", g_test.who, what,
                     chosen, event.index, box.kind, box.trap_no, box.item_no, event.radius, DistVector(event.pos, UserPos(3 - g_test.who)));
        return true;
    }

    if (o == "atla") {
        float best = 1.0e9f;

        for (int i = 0; i < 48; i++) {
            const DUNGEON_EVENT &event = NowDngMap->events[i];

            if (event.kind == DNG_EVENT_ATRA && NowDngMap->atra[event.index].used != 0 && DistVector((float *) event.pos, CharaMain.pos) < best) {
                best = DistVector((float *) event.pos, CharaMain.pos);
                g_test.event = i;
            }
        }

        if (g_test.event < 0) {
            std::fprintf(stderr, "duntest: no Atla on the floor\n");
            return false;
        }

        g_test.box = NowDngMap->events[g_test.event].index;
        sceVu0CopyVector(g_test.spot, NowDngMap->events[g_test.event].pos);
        PutAt(g_test.who, g_test.spot);
        std::fprintf(stderr, "duntest: put p%d on atla event %d (ball %d)\n", g_test.who, g_test.event, g_test.box);
        return true;
    }

    if (o == "gold" || o == "item" || o == "gatekey") {
        sceVu0FVECTOR at;

        sceVu0CopyVector(at, user);
        g_test.item = o == "item" ? (int) ITEM_BREAD : o == "gatekey" ? NowDngMap->gate_key_no : -1;
        RandomItem->Set(at, 101, o == "gold" ? 50 : -1, o == "gold" ? -1 : g_test.item);
        sceVu0CopyVector(g_test.spot, at);
        std::fprintf(stderr, "duntest: put p%d on %s: dropped at (%.0f %.0f %.0f), item %d, other %.0f away\n", g_test.who, what, at[0], at[1], at[2],
                     g_test.item, DistVector(at, UserPos(3 - g_test.who)));
        return true;
    }

    if (o == "lock" || o == "gate" || o == "lockmsg") {
        CDungeonEventData *lock = SlotNamed("gatekey");

        if (lock == nullptr) {
            std::fprintf(stderr, "duntest: no gate lock on the floor\n");
            return false;
        }

        g_test.item = NowDngMap->gate_key_no;

        CDungeonEventData *spot = lock;

        if (o == "lock") {
            ((CDngStatusData *) UserStatus)->GetItem(g_test.item, 0);

            // coop-gatekeyno (x-gate-flow -KeyNo): the floor's gate key number made N once the key is in the ring, the lock's
            // script unchanged, as Jep's pack 8 session had them disagree. Both copies, the same tick.
            COOP_TEST_SWITCH(int, keyno, TestEnvInt("DC_COOP_DUNTEST_KEYNO", -1)); // (round 4: read again at each job)

            if (keyno > 0) {
                std::fprintf(stderr, "duntest: the floor's gate key %d made %d (DC_COOP_DUNTEST_KEYNO), its lock unchanged\n", g_test.item, keyno);
                NowDngMap->gate_key_no = keyno;
            }
        } else if (o == "lockmsg") {
            // coop-p2prompt: no key in the pack, so the lock's Cross only says it is locked.
        } else {
            spot = WayOut(*lock);
            lock->Stop();

            if (spot == nullptr) {
                std::fprintf(stderr, "duntest: no way out on the floor\n");
                return false;
            }

            spot->Start();
        }

        g_test.event = (int) (spot - NowEventMan->event);
        sceVu0CopyVector(g_test.spot, spot->pos);
        SlotSpot(spot);
        std::fprintf(stderr, "duntest: put p%d on %s event %s (script %d, hold %d, switch %d), key %d\n", g_test.who, what, spot->event->name,
                     spot->event->script_no, spot->hold, spot->switch_on, g_test.item);
        return true;
    }

    if (std::strncmp(what, "event:", 6) == 0) {
        CDungeonEventData *spot = SlotNamed(what + 6);

        if (spot == nullptr) {
            std::fprintf(stderr, "duntest: no event %s on the floor\n", what + 6);
            return false;
        }

        g_test.event = (int) (spot - NowEventMan->event);
        sceVu0CopyVector(g_test.spot, spot->pos);
        SlotSpot(spot);
        std::fprintf(stderr, "duntest: put p%d on event %s (script %d, hold %d, switch %d)\n", g_test.who, spot->event->name, spot->event->script_no,
                     spot->hold, spot->switch_on);
        return true;
    }

    if (o == "trap") {
        // Gold in both purses, so a money circle has something to take.
        UserStatus->money = 100;
        PartnerSetGilda(100);

        for (MAP_TRAP_CIRCLE &circle : NowDngMap->trap_circle) {
            if (circle.state == 0) {
                sceVu0CopyVector(circle.pos, user);
                circle.state = 1;
                circle.kind = TestEnvInt("DC_COOP_DUNTEST_TRAP", 6);
                circle.timer = 0.0f;
                g_test.box = (int) (&circle - NowDngMap->trap_circle);
                std::fprintf(stderr, "duntest: put p%d on trap circle %d kind %d, other %.0f away\n", g_test.who, g_test.box, circle.kind,
                             DistVector(circle.pos, UserPos(3 - g_test.who)));
                return true;
            }
        }

        std::fprintf(stderr, "duntest: no free trap circle slot\n");
        return false;
    }

    if (o == "break") {
        const int    chara = g_test.who == 1 ? (int) UserStatus->cur_chara : g_partner.loaded_chara;
        WEAPON_HAVE &weapon = UserStatus->chara_weapons[chara][(int) UserStatus->equipped_weapon_slot[chara]];
        const int    was = weapon.item_no;

        weapon.durability_f = 0.01f;

        const int broke = g_test.who == 1 ? BattleSubWeaponDmg(5.0f, 0) : PartnerWeaponWear(5.0f, 0);

        g_test.item = was;
        std::fprintf(stderr, "duntest: put p%d on break: weapon %d of member %d -> %d (wear returned %d), message %d\n", g_test.who, was, chara,
                     weapon.item_no, broke, (int) DngMessMan.message);
        return true;
    }

    if (o == "spring") {
        for (int i = 0; i < 400; i++) {
            int parts_no = NowDngMap->cells[i].parts_no;

            if (parts_no != MAP_PARTS_NONE && NowDngMap->parts[parts_no].heal_on != 0) {
                const CDungeonParts &part = NowDngMap->parts[parts_no];
                float                at[4] = {part.heal_pos[0] + 160.0f * (i % 20), part.heal_pos[1] - 2.0f, part.heal_pos[2] + 160.0f * (i / 20), 1.0f};

                for (int chara = 0; chara < 6; chara++) {
                    UserStatus->water_now[chara] = 10.0f;
                }

                sceVu0CopyVector(g_test.spot, at);
                PutAt(g_test.who, at);
                std::fprintf(stderr, "duntest: put p%d in the healing zone of cell %d at (%.0f %.0f %.0f)\n", g_test.who, i, at[0], at[1], at[2]);
                return true;
            }
        }

        // No healing zone (Wise Owl Forest's first floor): the middle of the first part with water, under its surface.
        for (int i = 0; i < 400; i++) {
            int parts_no = NowDngMap->cells[i].parts_no;

            if (parts_no != MAP_PARTS_NONE && NowDngMap->parts[parts_no].water.used != 0) {
                const PARTS_WATER &water = NowDngMap->parts[parts_no].water;
                float              at[4] = {160.0f * (i % 20) + (water.vertex[0][0] + water.vertex[3][0]) / 2.0f, water.vertex[0][1] - 2.0f,
                                            160.0f * (i / 20) + (water.vertex[0][2] + water.vertex[3][2]) / 2.0f, 1.0f};

                for (int chara = 0; chara < 6; chara++) {
                    UserStatus->water_now[chara] = 10.0f;
                }

                sceVu0CopyVector(g_test.spot, at);
                PutAt(g_test.who, at);
                std::fprintf(stderr, "duntest: put p%d in the water of cell %d at (%.0f %.0f %.0f)\n", g_test.who, i, at[0], at[1], at[2]);
                return true;
            }
        }

        std::fprintf(stderr, "duntest: no healing zone or water on the floor\n");
        return false;
    }

    std::fprintf(stderr, "duntest: unknown object %s\n", what);
    return false;
}

std::string State() {
    char        text[512];
    std::string object;

    if (g_test.event >= 0 && g_test.box >= 0 && g_test.object != "atla" && g_test.object != "trap") {
        const TREASURE_BOX &box = NowDngMap->boxes[g_test.box];

        std::snprintf(text, sizeof text, "box closed %d used %d event %d lid %.0f", box.closed, box.used, NowDngMap->events[g_test.event].kind,
                      box.lid_angle);
        object = text;
    } else if (g_test.object == "atla" && g_test.box >= 0) {
        std::snprintf(text, sizeof text, "ball used %d event %d", NowDngMap->atra[g_test.box].used, NowDngMap->events[g_test.event].kind);
        object = text;
    } else if (g_test.object == "trap" && g_test.box >= 0) {
        std::snprintf(text, sizeof text, "circle state %d", NowDngMap->trap_circle[g_test.box].state);
        object = text;
    } else if (g_test.event >= 0) {
        const CDungeonEventData &slot = NowEventMan->event[g_test.event];

        std::snprintf(text, sizeof text, "slot hold %d switch %d", slot.hold, slot.switch_on);
        object = text;
    } else {
        object = "-";
    }

    g_test.object_now = object;

    CDungeonEventData *lock = SlotNamed("gatekey");
    int                p1 = UserStatus->cur_chara;
    int                p2 = g_partner.loaded_chara;

    // (coop-gatekeyrows: ring, the party's key ring wherever this copy keeps it; since coop-worldkeys a picked-up gate key goes
    // there, in neither player's bag, so the pack's count never moved for it)
    std::snprintf(text, sizeof text,
                  "task %d floor %d/%d %s; gold p1 %d p2 %d; item %d pack %d bag %d ring %d; map %d crystal %d; lock %d; water p1 %.0f p2 %.0f; hp p1 %d p2 %d; "
                  "busy %d down %d; apart %d; sysmes %d dngmes %d",
                  (int) gameTask, selectMapNo, (int) UserStatus->cur_floor, object.c_str(), (int) UserStatus->money, PartnerGilda(), g_test.item,
                  Count(UserStatus->item_pack, g_test.item), Count(PartnerPack(), g_test.item), RingCount(g_test.item), (int) BtEquipMap, (int) BtEquipMasuisyou,
                  lock != nullptr ? lock->switch_on : -1, UserStatus->water_now[p1], p2 >= 0 ? UserStatus->water_now[p2] : -1.0f,
                  (int) UserStatus->hp[p1], p2 >= 0 ? (int) UserStatus->hp[p2] : -1, g_partner.busy, g_partner.down, (int) CoopWorldsApart(), SystemMesNo,
                  (int) DngMessMan.message);
    return text;
}

// descend: DC_COOP_DUNTEST_FLOORS floors in all (default 3). After DC_COOP_DUNTEST_WAIT ticks of play on a floor (default 120;
// DC_COOP_DUNTEST_APART_WAIT apart, default 900) its gate's lock opens as its key opens it; together the user is put on the
// way out (the user's Cross, the test's presses, asks the other, whose answer DC_WORLDS_TEST_VOTE gives); apart this copy's own
// player takes the gate's script at once, as netplay's DC_NETPLAY_NEXT_FLOOR runs it.
void Descend(int floor) {
    static const int floors = TestEnvInt("DC_COOP_DUNTEST_FLOORS", 3);
    static const int wait = TestEnvInt("DC_COOP_DUNTEST_WAIT", 120);
    static const int apart_wait = TestEnvInt("DC_COOP_DUNTEST_APART_WAIT", 900);
    // coop-fx51d: DC_COOP_DUNTEST_WAIT=ready: together, the gate on the floor's first tick of play with the partner on it
    // (past its opening scene, and on a state fixture past its load) instead of after a tick count, once a floor or merge;
    // the copy says "ready at tick" (the link tick, the shortest line's anchor).
    static const bool wait_ready = TestEnvIs("DC_COOP_DUNTEST_WAIT", "ready");
    static const bool fixture = TestEnv("DC_STATE_LOAD_AT") != nullptr;
    static int       on_floor = -1;
    static int       ticks = 0;
    static bool      was_apart = false;
    static bool      ready_fired = false;
    const bool       apart = CoopWorldsApart();

    // coop-floorfollow: counted from the floor's start or the pair's merge on it (both copies on one link tick). The count is
    // not in the state set: the one caught up at a merge kept its own (its floor's play apart), opened its gate and put its
    // player on it 4 ticks into the lockstep, alone: OUT OF SYNC at the next checksum (x-dungeon-descent -Case ahead, F10).
    if (floor != on_floor || apart != was_apart) {
        on_floor = floor;
        was_apart = apart;
        ticks = 0;
        ready_fired = false;
    }

    if (gameTask != GAME_TASK_PLAY) {
        return;
    }

    // coop-b6monsters (x-floor7-zone): DC_COOP_DUNTEST_WAKE_LAST=D: on the last floor's first tick of play together, two
    // sleeping monsters put D either side of the player and woken, as Jep's floor 7 woke two as its scene ended (his
    // monsters 5 and 11, about 190 from him). Both copies, the same tick.
    static const int wake_last = TestEnvInt("DC_COOP_DUNTEST_WAKE_LAST", 0);

    if (++ticks == 1 && wake_last > 0 && !apart && NowMonstorUnit != nullptr && UserStatus->cur_floor + 1 >= floors) {
        int woken = 0;

        for (int i = 0; i < 16 && woken < 2; i++) {
            if (NowMonstorUnit->monster[i].state != 1 || NowMonstorUnit->monster[i].hp <= 0) {
                continue;
            }

            const float side = woken == 0 ? -1.0f : 1.0f;
            float       at[4] = {CharaMain.pos[0] - (float) wake_last, CharaMain.pos[1], CharaMain.pos[2] + side * 60.0f, 1.0f};

            NowMonstorUnit->chara[i][0].SetPosition(at);
            NowMonstorUnit->monster[i].state = 2;
            woken++;
            std::fprintf(stderr, "duntest: last floor: monster %d woken %d from p1 at (%.0f %.0f)\n", i, wake_last, at[0], at[2]);
        }
    }

    // coop-fx51d (x-dungeon-descent -Case ahead -Seamless on dun1-f1: under load the leaver's copy was still in its gate and the
    // next floor's opening scene when the follower's 900 ticks ran out, so the follower built a floor of its own, both took
    // themselves as there first and the merge went as a plain join): DC_COOP_DUNTEST_APART_WAIT=peer: apart, this copy's
    // gate once the other is heard playing a deeper floor of this dungeon (past its opening scene), not after a tick count.
    // =peerway: once the other is heard on its way onto the next floor (its stairs or the floor's opening scene, -2): the
    // follower sets out while the user is still on the way (x-dungeon-descent-ahead-seamless-p1~arrive_setout's case).
    static const bool apart_way = TestEnvIs("DC_COOP_DUNTEST_APART_WAIT", "peerway");
    static const bool apart_peer = TestEnvIs("DC_COOP_DUNTEST_APART_WAIT", "peer") || apart_way;
    static int        peer_fired = -1;

    if (apart && apart_peer) {
        // (loud when it never comes: 3600 ticks of play apart on this floor without the other heard deeper)
        if (peer_fired != floor && ticks == 3600) {
            std::fprintf(stderr, "duntest: descend: FAIL floor %d, apart: the other never heard playing a deeper floor in %d ticks\n",
                         (int) UserStatus->cur_floor, ticks);
        }
        const int peer = CoopWorldsPeerFloorIn(selectMapNo);

        if (peer_fired == floor || (apart_way ? peer != -2 : peer <= (int) UserStatus->cur_floor)) {
            return;
        }
        peer_fired = floor;
        std::fprintf(stderr, "duntest: descend: floor %d, apart: the other is heard %s (%d): this copy follows\n", (int) UserStatus->cur_floor,
                     apart_way ? "on its way onto the next floor" : "playing a deeper floor", peer);
    } else if (!apart && wait_ready) {
        if (ready_fired || FloorIntro() || g_partner.chara == nullptr || !g_partner.visible || NetplayStateLoadPending() ||
            (fixture && !NetplayStateFileLoaded())) {
            return;
        }
        ready_fired = true;
        std::fprintf(stderr, "duntest: descend: floor %d ready at tick %lld (playing together, the partner on the floor)\n", (int) UserStatus->cur_floor,
                     (long long) NetplayLinkTick());
    } else if (ticks != (apart ? apart_wait : wait)) {
        return;
    }

    if (UserStatus->cur_floor + 1 >= floors) {
        std::fprintf(stderr, "duntest: descend: floor %d is the last (player %d)\n", (int) UserStatus->cur_floor, NetplayLocalPlayer());
        return;
    }

    CDungeonEventData *lock = SlotNamed("gatekey");
    CDungeonEventData *out = lock != nullptr ? WayOut(*lock) : SlotNamed("out");

    if (out == nullptr || (!apart && g_partner.chara == nullptr)) {
        std::fprintf(stderr, "duntest: descend: no way out on floor %d (partner %d)\n", (int) UserStatus->cur_floor, g_partner.chara != nullptr ? 1 : 0);
        return;
    }

    if (lock != nullptr) {
        lock->Stop();
    }

    out->Start();

    if (apart) {
        BtEventInfo.script_no = out->event->script_no;
        BtEventInfo.ext_memory = out->event->ext_mem;
        BtEventInfo.event_marker = true;
        BtEventInfo.action_mode = 1;
        sceVu0CopyVector(BtEventInfo.position, out->pos);
        sceVu0CopyVector(BtEventInfo.direction, out->dir);
        gameTask = GAME_TASK_SCRIPT_START;
        std::fprintf(stderr, "duntest: descend: floor %d, apart: this copy's player (player %d) takes gate %s\n", (int) UserStatus->cur_floor,
                     NetplayLocalPlayer(), out->event->name);
        return;
    }

    PutAt(g_test.who, out->pos);
    std::fprintf(stderr, "duntest: descend: floor %d gate open, p%d put on %s (other %.0f away)\n", (int) UserStatus->cur_floor, g_test.who, out->event->name,
                 DistVector(out->pos, UserPos(3 - g_test.who)));
}

// coop-atlaapart: DC_COOP_DUNTEST_APART_ATLA=N (with descend): after N ticks of play apart on a floor, this copy's own player is
// (player by its role: the link is down apart) put on the floor's nearest Atla still there, once a floor; the copy's own Crosses (the test's pads) take it. From then each
// change (and every 60 ticks) "duntest: apart atla: ..." (x-atla-apart).
void ApartAtla(int floor, std::int64_t tick) {
    static const int at = TestEnvInt("DC_COOP_DUNTEST_APART_ATLA", -1);
    static int       on_floor = -1;
    static int       ticks = 0;
    static int       event = -1;
    static std::string last;
    static std::int64_t last_tick = 0;

    if (at < 0) {
        return;
    }

    if (floor != on_floor || !CoopWorldsApart()) {
        on_floor = floor;
        ticks = 0;
        event = -1;
        return;
    }

    if (event < 0 && gameTask == GAME_TASK_PLAY && ++ticks == at) {
        float best = 1.0e9f;

        for (int i = 0; i < 48; i++) {
            const DUNGEON_EVENT &e = NowDngMap->events[i];

            if (e.kind == DNG_EVENT_ATRA && NowDngMap->atra[e.index].used != 0 && DistVector((float *) e.pos, CharaMain.pos) < best) {
                best = DistVector((float *) e.pos, CharaMain.pos);
                event = i;
            }
        }

        if (event < 0) {
            std::fprintf(stderr, "duntest: apart atla: no Atla on floor %d\n", (int) UserStatus->cur_floor);
            return;
        }

        PutAt(1, NowDngMap->events[event].pos);
        std::fprintf(stderr, "duntest: apart atla: put player %d (chara %d) on atla event %d (ball %d), floor %d\n", NetplayRole(),
                     (int) UserStatus->cur_chara, event, NowDngMap->events[event].index, (int) UserStatus->cur_floor);
    }

    if (event < 0) {
        return;
    }

    char line[256];

    std::snprintf(line, sizeof line, "ball used %d event %d task %d chara %d sysmes %d dngmes %d", NowDngMap->atra[NowDngMap->events[event].index].used,
                  NowDngMap->events[event].kind, (int) gameTask, (int) UserStatus->cur_chara, SystemMesNo, (int) DngMessMan.message);

    if (line != last || tick - last_tick >= 60) {
        std::fprintf(stderr, "duntest: apart atla: tick %lld %s\n", (long long) tick, line);
        last = line;
        last_tick = tick;
    }
}

// coop-openpos: DC_COOP_DUNTEST_FROM=dx,dz puts the user that far from the spot instead of on it (an approach from one
// side, inside the object's reach), and DC_COOP_DUNTEST_OTHER=dx,dz the other player that far from the spot (standing by).
void PutOffsets() {
    for (const char *name : {"DC_COOP_DUNTEST_FROM", "DC_COOP_DUNTEST_OTHER"}) {
        const char *text = TestEnv(name);
        float       dx = 0.0f;
        float       dz = 0.0f;

        if (text == nullptr || std::sscanf(text, "%f,%f", &dx, &dz) != 2) {
            continue;
        }

        int   who = std::strcmp(name, "DC_COOP_DUNTEST_FROM") == 0 ? g_test.who : 3 - g_test.who;
        float at[4] = {g_test.spot[0] + dx, UserPos(who)[1], g_test.spot[2] + dz, 1.0f};

        PutAt(who, at);
        std::fprintf(stderr, "duntest: put p%d at (%.1f %.1f), (%.1f %.1f) from the spot\n", who, at[0], at[2], dx, dz);
    }
}

} // namespace

void DungeonTestSeed() {
    static const int seed = TestEnvInt("DC_DUNGEON_TEST_SEED", -1);

    if (seed < 0 || UserStatus == nullptr) {
        return;
    }

    PortRandSetState((unsigned int) seed + (unsigned int) UserStatus->cur_floor);
    std::fprintf(stderr, "duntest: floor %d built from the test seed %d\n", UserStatus->cur_floor, seed);
}

// coop-router (playtest items 16 and 21: "p2 attack a bat while p1 was opening atla"): DC_COOP_DUNTEST_WAKE=D: once the
// user is placed at the object, the floor's first sleeping monster is put D from the other player and woken, so the
// other fights while the user's presentation runs. Both copies, the same tick.
static void WakeBesideOther() {
    static const int distance = TestEnvInt("DC_COOP_DUNTEST_WAKE", 0);

    if (distance <= 0 || NowMonstorUnit == nullptr) {
        return;
    }

    for (int i = 0; i < 16; i++) {
        if (NowMonstorUnit->monster[i].state != 1 || NowMonstorUnit->monster[i].hp <= 0) {
            continue;
        }

        // DC_COOP_DUNTEST_WAKE_USER=1: beside the user instead, who is placed (the same spot every run).
        static const bool by_user = TestEnvOn("DC_COOP_DUNTEST_WAKE_USER");
        const int         whom = by_user ? g_test.who : 3 - g_test.who;
        const float      *beside = UserPos(whom);
        float             at[4] = {beside[0] + (float) distance, beside[1], beside[2], 1.0f};

        NowMonstorUnit->chara[i][0].SetPosition(at);
        NowMonstorUnit->monster[i].state = 2;
        std::fprintf(stderr, "duntest: monster %d woken %d from p%d at (%.0f %.0f)\n", i, distance, whom, at[0], at[2]);
        return;
    }

    std::fprintf(stderr, "duntest: no sleeping monster to wake\n");
}

// coop-chestworld: player 1's first quick slot holds count of item, its model loaded as the floor's start loads them
// (BtSetActiveItemModel), and in his hand (itemNowSel, as his Left/Right pick it). Both copies, the same tick.
static void QuickSlot(int item, int count) {
    ITEM_PACK &pack = UserStatus->item_pack;

    pack.quick_item_slot[0] = (s16) item;
    pack.quick_item_qty[0] = (s16) count;
    BtSetActiveItemModel(read_buffer);
    itemNowSel = 1;
    activeItem.now = 1;
    std::fprintf(stderr, "duntest: p1 quick slot 1 holds %d of item %d\n", count, item);
}

// coop-chestworld: DC_COOP_DUNTEST_DRINK=1: as the user (player 1) is placed, a Stamina Drink in his hand (his Square drinks
// it: retail's drink, which held the world). DC_COOP_DUNTEST_REVIVE=T: at press clock T, a Revival Powder in his first
// quick slot and his life to 0, so retail's GAME_TASK_DEAD revives him (REVIVE_WAIT, then REVIVE_DONE until his Cross).
static void PlayerOneItems(std::int64_t tick, std::int64_t clock, bool placed) {
    static const bool drink = TestEnvOn("DC_COOP_DUNTEST_DRINK");
    static const int  revive_at = TestEnvInt("DC_COOP_DUNTEST_REVIVE", -1);
    static bool       revived = false;
    // (coop-warmconvert, WS7: once a job on a warm pair, as once a fresh copy; trace_once_kept twin: once a process)
    COOP_TEST_TRACE_RESET(revived, false);

    if (g_test.who != 1 || UserStatus == nullptr) {
        return;
    }
    if (drink && placed) {
        QuickSlot(ITEM_STAMINA_DRINK, 3);
    }
    // His action, each tick it changes (3: drinking; a drink's length is its motion's, in no table), with his life then.
    static int last_act = -1;

    if (drink && BtActStatus.action_on != last_act) {
        last_act = BtActStatus.action_on;
        std::fprintf(stderr, "duntest: p1 act %d at tick %lld clock %lld life %d\n", last_act, (long long) tick, (long long) clock,
                     (int) UserStatus->hp[UserStatus->cur_chara]);
    }
    if (revive_at >= 0 && !revived && clock >= revive_at && gameTask == GAME_TASK_PLAY) {
        revived = true;
        QuickSlot(ITEM_REVIVAL_POWDER, 1);
        UserStatus->hp[UserStatus->cur_chara] = 0;
        std::fprintf(stderr, "duntest: p1 knocked out at clock %lld with a Revival Powder\n", (long long) clock);
    }
}

// coop-chestworld: with DC_COOP_DUNTEST_WAKE, every 10 ticks of the link once placed, each awake monster's place, life and
// motion, the holds, the game's task, player 1's action and whether his menu is open (co-op's beside the world, or retail's
// BT_GAME_MODE_BATTLE_MENU) ("duntest: monsters tick L clock C hold H task K act A menu M: i (x z) hp N motion M; ..."):
// whether the world ran.
static void MonsterLines(std::int64_t tick, std::int64_t clock) {
    static const int distance = TestEnvInt("DC_COOP_DUNTEST_WAKE", 0);

    if (distance <= 0 || NowMonstorUnit == nullptr || !g_test.put || tick % 10 != 0) {
        return;
    }

    std::string line;
    char        part[96];

    for (int i = 0; i < 16; i++) {
        if (NowMonstorUnit->monster[i].state != 2) {
            continue;
        }

        const float *pos = NowMonstorUnit->chara[i][0].pos;

        std::snprintf(part, sizeof part, "%d (%.1f %.1f) hp %d motion %d; ", i, pos[0], pos[2], (int) NowMonstorUnit->monster[i].hp,
                      (int) NowMonstorUnit->chara[i][0].motion_no);
        line += part;
    }

    const int menu = FullMenuOpenFor(1) || BtGameModeFlag == BT_GAME_MODE_BATTLE_MENU ? 1 : 0;

    std::fprintf(stderr, "duntest: monsters tick %lld clock %lld hold %d task %d act %d menu %d: %s\n", (long long) tick, (long long) clock,
                 (int) CMonUnitHold, (int) gameTask, (int) BtActStatus.action_on, menu, line.c_str());
}

// coop-soakfix3 (DC_COOP_DUNTEST_ALONE=1, tests/test_fixture_join.py::test_host_sits): a host alone, not linked (no press clock, no
// partner), is put on DC_COOP_DUNTEST's object as player 1 once, on the first play frame after his fixture's load; his own input
// script then uses it, before a joiner starts. Says "duntest: alone: ...".
void DunTestAloneFrame() {
    static const bool alone = TestEnvOn("DC_COOP_DUNTEST_ALONE");
    static bool       done = false;
    const char       *object = TestEnv("DC_COOP_DUNTEST");

    if (!alone || done || object == nullptr || NetplayPartnerLinked() || !NetplayStateFileLoaded() || mode != GAME_MODE_DUNGEON ||
        gameTask != GAME_TASK_PLAY || NowDngMap == nullptr || NowEventMan == nullptr || UserStatus == nullptr || RandomItem == nullptr) {
        return;
    }
    done = true;
    g_test.object = object;
    g_test.who = 1;
    g_test.floor = selectMapNo * 256 + UserStatus->cur_floor;
    g_test.put = true;
    // revive: a Revival Powder in his first active slot and his life to nothing, so the game's own knockout finds it
    // (GAME_TASK_DEAD -> REVIVE_WAIT -> REVIVE_DONE, its message till his Cross).
    if (g_test.object == "revive") {
        CDngStatusData *status = (CDngStatusData *) UserStatus;

        status->item_pack.quick_item_slot[0] = ITEM_REVIVAL_POWDER;
        status->item_pack.quick_item_qty[0] = 1;
        UserStatus->hp[UserStatus->cur_chara] = 0;
        std::fprintf(stderr, "duntest: alone: p1 knocked out with a Revival Powder in his first active slot\n");
        return;
    }
    const bool placed = Place();

    std::fprintf(stderr, "duntest: alone: p1 on %s: %s\n", object, placed ? "placed" : "FAIL not placed");
}

void DunTestTick(std::int64_t tick) {
    static const char *object = TestEnv("DC_COOP_DUNTEST");
    static int         generation = TestJobGeneration();

    // coop-serve: a persistent client's next job drives its own object (its "env" lines): read again, the driver afresh
    if (generation != TestJobGeneration()) {
        generation = TestJobGeneration();
        object = TestEnv("DC_COOP_DUNTEST");
        g_test = decltype(g_test){};
    }

    if (object == nullptr || mode != GAME_MODE_DUNGEON || NowDngMap == nullptr || NowEventMan == nullptr || UserStatus == nullptr ||
        RandomItem == nullptr) {
        return;
    }

    if (g_test.object.empty()) {
        g_test.object = object;
        g_test.who = TestEnvInt("DC_COOP_DUNTEST_WHO", 2) == 1 ? 1 : 2;
        g_test.at = TestEnvInt("DC_COOP_DUNTEST_AT", 20);
        g_test.both = TestEnvOn("DC_COOP_DUNTEST_BOTH");
    }

    const int floor = selectMapNo * 256 + UserStatus->cur_floor;

    if (floor != g_test.floor && gameTask == GAME_TASK_PLAY) {
        g_test.floor = floor;
        g_test.put = false;
        g_test.event = g_test.box = -1;
        g_test.target = nullptr;
        g_test.check = -1;
        FloorLines();
    }

    const std::int64_t clock = NetplayTestPressClock();

    if (g_test.object == "descend") {
        Descend(floor);
        ApartAtla(floor, tick);
    } else if (!g_test.put && !g_test.swapped && TestEnvOn("DC_COOP_DUNTEST_SWAP") && clock >= g_test.at && gameTask == GAME_TASK_PLAY &&
               g_partner.chara != nullptr && g_test.floor == floor) {
        // coop-atlaswap (Jep's 2026-11-02 playtest, Atla after a character swap): the pair swaps members first, as her Characters
        // pick and his yes do (Trade), on both copies at the same tick; the user is placed once both members are in.
        g_test.swapped = true;
        g_test.swap_player = g_partner.loaded_chara;
        g_test.swap_partner = UserStatus->cur_chara;
        const bool traded = PromptTradeForTest();

        std::fprintf(stderr, "duntest: swap at tick %lld: player %d, partner %d (%s)\n", (long long) tick, g_test.swap_player, g_test.swap_partner,
                     traded ? "traded" : "refused");
    } else if (!g_test.put && clock >= g_test.at && gameTask == GAME_TASK_PLAY && g_partner.chara != nullptr && g_test.floor == floor &&
               (!TestEnvOn("DC_COOP_DUNTEST_SWAP") ||
                (g_test.swapped && UserStatus->cur_chara == g_test.swap_player && g_partner.loaded_chara == g_test.swap_partner))) {
        g_test.put = true;

        const bool placed = Place();

        if (placed && g_test.placed_floor < 0) {
            g_test.placed_floor = floor;
        }

        if (placed && g_test.both) {
            g_test.second = (int) clock + 1;
        }
        if (placed && TestEnv("DC_COOP_DUNTEST_ALSO") != nullptr) {
            g_test.other_at = (int) clock + 1;
        }

        // coop-b4story: DC_COOP_DUNTEST_BOX_BESIDE=1: a small box with a Bread dropped 20 beside the other player as the user is
        // placed (on both copies, as a script's _SET_IBOX drops B4's miniboss boxes), so the other has a box of its own in sight
        // through the user's opening.
        if (placed && TestEnvOn("DC_COOP_DUNTEST_BOX_BESIDE")) {
            const float  *other = UserPos(3 - g_test.who);
            sceVu0FVECTOR beside = {other[0] + 20.0f, other[1], other[2], 1.0f};

            std::fprintf(stderr, "duntest: a box beside p%d at (%.0f %.0f %.0f): event %d\n", 3 - g_test.who, beside[0], beside[1], beside[2],
                         NowDngMap->SetTreasureBox(beside, ITEM_BREAD, TREASURE_BOX_SMALL, 0));
        }

        if (placed) {
            PutOffsets();
            WakeBesideOther();
            PlayerOneItems(tick, clock, true); // coop-chestworld
        }

        if (placed && TestEnvOn("DC_COOP_DUNTEST_POISON")) {
            const int chara = g_test.who == 1 ? (int) UserStatus->cur_chara : g_partner.loaded_chara;

            UserStatus->ailments[chara] |= AILMENT_POISON;
            std::fprintf(stderr, "duntest: p%d poisoned at tick %lld (member %d)\n", g_test.who, (long long) tick, chara);
        }
    }

    // coop-p2prompt: DC_COOP_DUNTEST_QUIT=N: N link ticks after the object's own state first differs from the placement's (or the
    // play starts on another floor), this copy quits, so a test ends once its result is decided.
    static const int quit = TestEnvInt("DC_COOP_DUNTEST_QUIT", -1);

    // (coop-fx51a: the decision is said whether or not the copy quits on it, so a job on a running pair (DC_QA_SERVE) can end
    // on it without quitting the copy, which serves the next job)
    if (g_test.placed_floor >= 0) {
        if (g_test.decided < 0 && (g_test.floor != g_test.placed_floor || (!g_test.object_first.empty() && g_test.object_now != g_test.object_first))) {
            g_test.decided = tick;
            std::fprintf(stderr, "duntest: decided at tick %lld: %s -> %s (floor %d)\n", (long long) tick, g_test.object_first.c_str(),
                         g_test.object_now.c_str(), floor);
        }
    }
    if (quit >= 0 && g_test.placed_floor >= 0) {
        static bool quit_sent = false;
        // (coop-warmconvert, WS7: once a job on a warm pair, as once a fresh copy; trace_once_kept twin: once a process)
        COOP_TEST_TRACE_RESET(quit_sent, false);

        if (g_test.decided >= 0 && tick >= g_test.decided + quit && !quit_sent) {
            quit_sent = true;
            // (coop-warmconvert: a warm pair's copy (DC_QA_SERVE) serves the next job: the job ends here, the copy runs on)
            if (TestEnv("DC_QA_SERVE") != nullptr) {
                std::fprintf(stderr, "duntest: %d ticks after the decision: the job's run is over\n", quit);
            } else {
                SDL_Event event{};

                event.type = SDL_EVENT_QUIT;
                SDL_PushEvent(&event);
                std::fprintf(stderr, "duntest: quits %d ticks after the decision\n", quit);
            }
        }
    }

    Kept(clock);
    MonsterLines(tick, clock); // coop-chestworld
    if (g_test.put) {
        PlayerOneItems(tick, clock, false); // coop-chestworld
    }
    if (g_test.other_at >= 0 && clock >= g_test.other_at && gameTask == GAME_TASK_PLAY) {
        const DunTest kept = g_test;

        g_test.object = TestEnv("DC_COOP_DUNTEST_ALSO");
        g_test.who = 3 - kept.who;
        const bool placed = Place();

        std::fprintf(stderr, "duntest: other p%d at %s: %s\n", g_test.who, g_test.object.c_str(), placed ? "placed" : "none on the floor");
        g_test = kept;
        g_test.other_at = -1;
    }

    if (static int again = TestEnvInt("DC_COOP_DUNTEST_AGAIN", -1); again >= 0 && clock >= again && gameTask == GAME_TASK_PLAY) {
        again = -1;
        const bool placed = Place();

        std::fprintf(stderr, "duntest: again p%d at %s: %s\n", g_test.who, g_test.object.c_str(), placed ? "placed" : "none on the floor");
    }

    if (g_test.second >= 0 && clock >= g_test.second) {
        g_test.second = -1;
        PutAt(3 - g_test.who, g_test.spot);
        std::fprintf(stderr, "duntest: put p%d on the same spot\n", 3 - g_test.who);
    }

    if (g_test.floor != floor) {
        return;
    }

    const std::string now = State();

    if (g_test.put && g_test.object_first.empty() && floor == g_test.placed_floor) {
        g_test.object_first = g_test.object_now;
    }

    if (now != g_test.last || tick - g_test.last_tick >= 20) {
        std::fprintf(stderr, "duntest: tick %lld clock %lld p1 (%.0f %.0f) p2 (%.0f %.0f) %s\n", (long long) tick, (long long) clock, CharaMain.pos[0],
                     CharaMain.pos[2], g_partner.pos[0], g_partner.pos[2], now.c_str());
        g_test.last = now;
        g_test.last_tick = tick;
    }
}

} // namespace coop
