#include "coop/game_ns.hpp"
#include <libvu0.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "btactstatus.hpp"
#include "coop/coop.hpp"
#include "coop/coop_core.hpp"
#include "coop/coop_actor.hpp"  // coop-downmenu: kMaxPlayers
#include "coop/coop_combat.hpp" // coop-downmenu: PlayerDown
#include "coop/netplay.hpp"     // coop-downtarget: NetplayLinkTick
#include "dun/gameloop.hpp"
#include "mathutil.hpp"
#include "monstorunit.hpp"
#include "runscript.hpp"
#include "runscript_opcodes.hpp"
#include "userstatus.hpp"

// The monster script opcodes that read where the player stands, retail's (ps2/src/runscript_opcodes.cpp)
// with CharaMain's position replaced by that of whichever co-op player the monster goes after. Without co-op, or
// with the partner off the floor, that is always the player; a player lying down is no one's (coop-downmenu).

namespace {

// Distance the other player has to be nearer by before a monster turns to it, so that one standing
// between the two does not swap every tick.
constexpr float kSwitchMargin = 15.0f;

// Whom each monster goes after: a player index (player number - 1): 0 the player, 1 the partner.
int g_target[16];

int GetStackInt(RS_STACKDATA *argument) {
    if (argument->type == RS_FLOAT) {
        return (int) argument->f;
    }

    return argument->i;
}

float GetStackFloat(RS_STACKDATA *argument) {
    if (argument->type == RS_INT) {
        return (float) argument->i;
    }

    return argument->f;
}

void SetStack(RS_STACKDATA *argument, int value) {
    if (argument->type == RS_PTR) {
        argument->p->i = value;
    }
}

void SetStack(RS_STACKDATA *argument, float value) {
    if (argument->type == RS_PTR) {
        argument->p->f = value;
    }
}

struct Target {
    float position[4];
    float heading[4];
    int   since_attack;
    int   chara;
    bool  partner;
};

// The body of player index + 1 as a monster's target: where it stands, its heading, its swing timer and member. False while it cannot be
// gone after: no body on the floor (players past 2 have none yet), or lying down.
// coop-downmenu (the developer's rc5b playtest, 11-19: "enemies dont stop targeting dead players"): the pick began from player 1 whatever he was and
// asked only the partner whether she stood (CoopPartnerTarget), so with him down every monster near him kept going after him, swinging at
// a body its blows cannot hurt, while she stood by untouched. Retail never had a player lying down in play (its down is the game over),
// so its scripts never asked. Now a player lying down is no one's target; a revived one is again. With every player down the game's death
// runs that tick (PlayerDownStep: the game over), and the monsters read player 1 as retail's do through it.
// DC_COOP_MUTATE=target_down (twin): a downed player is a target as before (either one).
bool TargetFor(int index, Target &out) {
    if (index == 0) {
        sceVu0CopyVector(out.position, CharaMain.pos);
        sceVu0CopyVector(out.heading, BtActStatus.input_direction);
        out.since_attack = BtActStatus.frames_since_attack;
        out.chara = UserStatus->cur_chara;
        out.partner = false;
        return !coop::PlayerDown(1) || coop::Mutated("target_down");
    }
    if (index == 1) {
        out.partner = true;
        if (CoopPartnerTarget(out.position, out.heading, &out.since_attack, &out.chara)) {
            return true;
        }
        if (!coop::Mutated("target_down") || !coop::PlayerDown(2)) {
            return false;
        }
        sceVu0CopyVector(out.position, coop::g_partner.pos); // (the twin: down, still gone after)
        sceVu0CopyVector(out.heading, coop::g_partner.heading);
        out.since_attack = coop::g_partner.since_attack;
        out.chara = coop::g_partner.loaded_chara;
        return true;
    }
    return false;
}

// The player the current monster goes after: the nearest it can go after, held until another is nearer by kSwitchMargin (or the one
// held can no longer be gone after).
Target TargetOf(int monster_no) {
    Target players[coop::kMaxPlayers];
    bool   can[coop::kMaxPlayers];
    float  distance[coop::kMaxPlayers] = {};
    int    any = 0;

    for (int i = 0; i < coop::kMaxPlayers; i++) {
        can[i] = TargetFor(i, players[i]);
        any += can[i] ? 1 : 0;
    }

    if (monster_no < 0 || monster_no >= 16) {
        return players[0];
    }
    if (any == 0) { // every player down: retail's player (the game's death runs)
        g_target[monster_no] = 0;
        return players[0];
    }

    float monster_position[4];

    NowMonstorUnit->chara[monster_no][0].GetPosition(monster_position);

    int nearest = -1;

    for (int i = 0; i < coop::kMaxPlayers; i++) {
        if (can[i]) {
            distance[i] = DistVector(monster_position, players[i].position);
            nearest = nearest < 0 || distance[i] < distance[nearest] ? i : nearest;
        }
    }

    int held = g_target[monster_no];

    if (held < 0 || held >= coop::kMaxPlayers || !can[held] || distance[nearest] + kSwitchMargin < distance[held]) {
        held = nearest;
    }
    g_target[monster_no] = held;
    return players[held];
}

} // namespace

void CoopTargetReset() {
    for (int &target : g_target) {
        target = 0;
    }
}

// coop-downtarget (the developer's playtest 3: "if a monster is targeting a player that dies, that monster will switch to another player and try to
// attack them no matter how far away they are instead of following normal distance rules"): retail ends a chase by the monster's sleep
// past its wake distance + 10 of its player (CheckViewLevel below). A downed player's body keeps the monsters round it awake
// (coop-dungeonp2, OLD-50: they froze and vanished), and an awake monster's script goes after the player it reads (TargetOf: a standing
// one) however far: the downtrace showed it come from 658 to 353 of her across the floor. A monster kept up only by a downed body (no
// standing player within its wake distance + 10) now idles: its think waits (CMonstorUnit's step, recipe 0061) and its body stands in
// its first motion; once a standing player is within reach its think starts again from its own beginning, as from a sleep.
// DC_COOP_MUTATE=downtarget_keeps_chase (twin): its think goes on, after the far player.
bool CoopMonsterIdles(int monster_no) {
    static bool idle[16] = {};

    if (!coop::Enabled() || NowMonstorUnit == nullptr || monster_no < 0 || monster_no >= 16 || coop::Mutated("downtarget_keeps_chase")) {
        return false;
    }
    MONSTOR &monster = NowMonstorUnit->monster[monster_no];
    bool     now = false;

    if (monster.state == 2 && monster.hp > 0) {
        float at[4];

        NowMonstorUnit->chara[monster_no][0].GetPosition(at);
        bool near = false;

        for (int i = 0; i < coop::kMaxPlayers && !near; i++) {
            Target player;

            near = TargetFor(i, player) && DistVector(at, player.position) <= 10.0f + monster.clip_distance;
        }
        now = !near;
    }
    // (stateless, so a copy that joined by the state set does as the others: idle, its think kept at its beginning and its body in its
    // first motion; the edges only logged)
    if (now) {
        NowMonstorUnit->script_state[monster_no] = 0;
        for (int j = 0; j <= monster.attachment_count; j++) {
            if (NowMonstorUnit->chara[monster_no][j].motion_no != 0) {
                NowMonstorUnit->chara[monster_no][j].SetMotion(0, 1);
            }
        }
    }
    if (now != idle[monster_no]) {
        idle[monster_no] = now;
        if (CoopDebug()) {
            std::printf("coop-target: monster %d %s, link tick %lld\n", monster_no,
                        now ? "idles: no standing player within its reach (a downed one keeps it up)" : "thinks again: a standing player within its reach",
                        (long long) NetplayLinkTick());
        }
    }
    return now;
}

int CoopMonsterTarget(int monster_no) {
    return monster_no >= 0 && monster_no < 16 ? g_target[monster_no] : 0;
}

namespace dc::game { // repin: was PORT_OVERRIDE; a recipe removes the game's own definition
int _GET_DISTANCE(RS_STACKDATA *stack, int argc) {
    int   monster_no = NowMonstorUnit->current_monster;
    float monster_position[4];
    float target_position[4];

    NowMonstorUnit->chara[monster_no][0].GetPosition(monster_position);

    if (argc == 1) {
        sceVu0CopyVector(target_position, TargetOf(monster_no).position);
    } else {
        target_position[0] = GetStackFloat(stack++);
        target_position[1] = GetStackFloat(stack++);
        target_position[2] = GetStackFloat(stack++);
    }

    SetStack(stack, DistVector(monster_position, target_position));
    return 1;
}
} // namespace dc::game

namespace dc::game { // repin: was PORT_OVERRIDE; a recipe removes the game's own definition
int _GET_POSITION(RS_STACKDATA *stack, int argc) {
    int   monster_no = NowMonstorUnit->current_monster;
    int   target = (int) GetStackFloat(stack++);
    float position[4];

    if (target == -1) {
        NowMonstorUnit->chara[monster_no][0].GetPosition(position);
    }

    if (target == -2) {
        sceVu0CopyVector(position, TargetOf(monster_no).position);
    }

    SetStack(stack++, position[0]);
    SetStack(stack++, position[1]);
    SetStack(stack, position[2]);
    return 1;
}
} // namespace dc::game

// Retail's falls off its end without a return value; the port's returns 1, as every other opcode
// that goes on with the script does.
namespace dc::game { // repin: was PORT_OVERRIDE; a recipe removes the game's own definition
int _CHK_USER_INNER_PRODUCT(RS_STACKDATA *stack, int argc) {
    int   in_view;
    int   monster_no = NowMonstorUnit->current_monster;
    float min_cosine;
    float max_distance;
    float position[4];
    float rotation[4];
    float player[4];
    float away_from_player[4];

    in_view = 0;
    min_cosine = 1.0f - 0.011111111f * GetStackInt(stack++);
    max_distance = 100000;

    if (argc == 3) {
        max_distance = GetStackFloat(stack++);
    }

    sceVu0CopyVector(player, TargetOf(monster_no).position);
    NowMonstorUnit->chara[monster_no][0].GetPosition(position);
    NowMonstorUnit->chara[monster_no][0].GetRotation(rotation);
    float direction[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    float identity[4][4];
    float facing_matrix[4][4];

    sceVu0UnitMatrix(identity);
    sceVu0RotMatrixY(facing_matrix, identity, rotation[1]);
    sceVu0ApplyMatrix(direction, facing_matrix, direction);
    sceVu0Normalize(direction, direction);
    away_from_player[0] = position[0] - player[0];
    away_from_player[2] = position[2] - player[2];
    away_from_player[1] = 0.0f;
    away_from_player[3] = 1.0f;
    sceVu0Normalize(away_from_player, away_from_player);

    if (sceVu0InnerProduct(direction, away_from_player) >= min_cosine && DistVector(position, player) < max_distance) {
        in_view = 1;
    }

    SetStack(stack, in_view);
    return 1;
}
} // namespace dc::game

// For the partner, its own heading, swing timer and character stand in for the player's input
// direction, BtActStatus.frames_since_attack and the character in control.
namespace dc::game { // repin: was PORT_OVERRIDE; a recipe removes the game's own definition
int _GUARD_SEARCH(RS_STACKDATA *stack, int argc) {
    int    monster_no = NowMonstorUnit->current_monster;
    Target target = TargetOf(monster_no);
    float  position[4];
    float  direction[4];
    float  distance;

    NowMonstorUnit->chara[monster_no][0].GetPosition(position);
    distance = DistVector(target.position, position);
    direction[0] = position[0] - target.position[0];
    direction[1] = 0.0f;
    direction[2] = position[2] - target.position[2];
    direction[3] = 1.0f;
    sceVu0Normalize(direction, direction);

    if (target.partner) {
        if (sceVu0InnerProduct(target.heading, direction) >= 0.35) {
            target.since_attack = 3600;
        }
    } else if (sceVu0InnerProduct(BtActStatus.input_direction, direction) >= 0.35) {
        BtActStatus.frames_since_attack = 3600;
        target.since_attack = 3600;
    }

    SetStack(stack++, target.since_attack);
    SetStack(stack++, distance);
    SetStack(stack, target.chara);
    return 1;
}
} // namespace dc::game

// Retail's, with each monster's distance taken to the nearer of the two players: a monster wakes
// (state 2) when either comes within its clip distance and sleeps when both are past it, and the four
// nearest awake kept when more are up are the four nearest to either.
namespace dc::game { // repin: was PORT_OVERRIDE; a recipe removes the game's own definition
void CMonstorUnit::CheckViewLevel() {
    sceVu0FVECTOR player_position;
    sceVu0FVECTOR partner_position;
    sceVu0FVECTOR monster_position;
    int           sorted[16];
    int           active[16];
    sceVu0CopyVector(player_position, CharaMain.pos);
    // coop-dungeonp2: the partner counts while she is on the floor, standing or down. Waking only by a
    // standing partner put the monsters round a downed one to sleep: they froze, and on her copy, far from
    // the player, stopped drawing (Jep: "enemies freeze and vanish when P2 dies"). Monsters still go after
    // her only while she stands (CoopPartnerTarget). DC_COOP_MUTATE=viewdown is the twin.
    bool partner = CoopPartnerTarget(partner_position, nullptr, nullptr, nullptr);

    if (!partner && coop::Enabled() && coop::g_partner.visible && coop::g_partner.down > 0 && !coop::Mutated("viewdown")) {
        sceVu0CopyVector(partner_position, coop::g_partner.pos);
        partner = true;
    }

    int active_count = 0;

    for (int i = 0; i < 16; i++) {
        active[i] = -1;
    }

    for (int i = 0; i < 16; i++) {
        sorted[i] = -1;
        monster[i].view_held = false;

        if (monster[i].state == -1 || monster[i].revealed == 0) {
            continue;
        }

        CCharacter *character = &chara[i][0];
        character->GetPosition(monster_position);
        monster_position[3] = 1;
        player_position[3] = 1;
        partner_position[3] = 1;
        float distance = DistVector(player_position, monster_position);

        bool  by_partner = false;

        if (partner) {
            float to_partner = DistVector(partner_position, monster_position);

            by_partner = to_partner < distance;
            distance = std::min(distance, to_partner);
        }

        monster[i].player_distance = distance;

        if (monster[i].state == 1 && distance < monster[i].clip_distance) {
            monster[i].state = 2;

            if (by_partner && CoopDebug()) {
                std::printf("coop-debug: monster %d woken by the partner at %.0f\n", i, distance);
            }
        }

        if (monster[i].state == 2 && distance > 10.0f + monster[i].clip_distance) {
            monster[i].state = 1;
        }

        if (monster[i].hp <= 0) {
            monster[i].state = 2;
        }

        if (monster[i].state == 2) {
            active[active_count] = i;
            active_count++;
        }
    }

    int sorted_count = 0;

    if (active_count > 4) {
        int floor = UserStatus->cur_floor;
        int max_floor = maxFloorTbl__3[selectMapNo];

        if (floor + 1 == max_floor) {
            return;
        }

        for (int i = 0; i < active_count; i++) {
            int   closest = -1;
            float distance = 3200.0f;

            for (int j = 0; j < active_count; j++) {
                if (active[j] != -1 && !(distance <= monster[active[j]].player_distance)) {
                    distance = monster[active[j]].player_distance;
                    closest = j;
                }
            }

            if (closest != -1) {
                sorted[sorted_count] = active[closest];
                active[closest] = -1;
                sorted_count++;
            }
        }

        for (int i = 4; i < active_count; i++) {
            if (sorted[i] != -1 && monster[sorted[i]].state == 2 && monster[sorted[i]].hp > 0 && monster[sorted[i]].kind != MONSTER_KIND_NO_LOCK_ON) {
                monster[sorted[i]].state = 1;
                monster[sorted[i]].view_held = true;
            }
        }
    }
}
} // namespace dc::game
// coop: gaps2
// Retail's, with the partner as a second body: a monster stepping into either player stops, so the
// monsters crowd round the partner as they do round the player instead of walking through it. The
// partner counts while it is on the floor and standing (CoopPartnerTarget). DC_COOP_MUTATE=monster_body
// leaves the partner out, for the test's twin.
namespace {

// Whether the current monster's step from position by displacement runs into a player standing at
// player_position (retail's test: within 6 units plus its body radius, below the player's head, and
// heading toward it).
bool StepsInto(const MONSTOR &monster, const float *position, const float *next_position, const float *displacement,
               const float *player_position) {
    sceVu0FVECTOR flat_player;
    sceVu0FVECTOR flat_next;
    sceVu0FVECTOR towards_player;

    sceVu0CopyVector(flat_player, (float *) player_position);
    sceVu0CopyVector(flat_next, (float *) next_position);
    flat_player[1] = 1;
    flat_next[1] = 1;

    if (!(DistVector(flat_player, flat_next) <= 6.0f + monster.body_radius && next_position[1] < 18.0f + player_position[1])) {
        return false;
    }

    towards_player[0] = player_position[0] - position[0];
    towards_player[2] = player_position[2] - position[2];
    towards_player[1] = 0;
    towards_player[3] = 1;
    sceVu0Normalize(towards_player, towards_player);
    return !(sceVu0InnerProduct((float *) displacement, towards_player) <= 0.0f);
}

} // namespace

namespace dc::game { // repin: was PORT_OVERRIDE; a recipe removes the game's own definition
void CMonstorUnit::MoveCheck2() {
    sceVu0FVECTOR player_position;
    sceVu0FVECTOR partner_position;
    sceVu0FVECTOR next_position;
    sceVu0FVECTOR position;
    sceVu0FVECTOR displacement;
    MONSTOR      &self = monster[current_monster];

    sceVu0CopyVector(player_position, CharaMain.pos);
    chara[current_monster][0].GetPosition(position);
    next_position[0] = position[0] + self.movement[0] * self.movement_speed;
    next_position[1] = position[1] + self.movement[1] * self.movement_speed;
    next_position[2] = position[2] + self.movement[2] * self.movement_speed;
    displacement[0] = next_position[0] - position[0];
    displacement[1] = 0;
    displacement[2] = next_position[2] - position[2];
    displacement[3] = 1;
    sceVu0Normalize(displacement, displacement);

    bool stopped = StepsInto(self, position, next_position, displacement, player_position);

    if (!stopped && !coop::Mutated("monster_body") && CoopPartnerTarget(partner_position, nullptr, nullptr, nullptr)) {
        stopped = StepsInto(self, position, next_position, displacement, partner_position);
    }

    if (stopped) {
        self.movement[0] = 0;
        self.movement[1] = 0;
        self.movement[2] = 0;
        self.movement_speed = 0;
    }
}
} // namespace dc::game

// coop: gaps2
// The character in control and its ailment time, as a monster script reads them (retail's take
// UserStatus->cur_chara): for a monster after the partner, the partner's member and its own ailment.
// DC_COOP_MUTATE=chrid answers with the player's, as before, for the test's twin.
namespace dc::game { // repin: was PORT_OVERRIDE; a recipe removes the game's own definition
int _GET_CHR_ID(RS_STACKDATA *stack, int argc) {
    int chara = UserStatus->cur_chara;

    if (!coop::Mutated("chrid")) {
        chara = TargetOf(NowMonstorUnit->current_monster).chara;
    }

    SetStack(stack, chara);
    return 1;
}
} // namespace dc::game

namespace dc::game { // repin: was PORT_OVERRIDE; a recipe removes the game's own definition
int _GET_USER_STATUS(RS_STACKDATA *stack, int argc) {
    int kind = GetStackInt(stack++);
    int chara = UserStatus->cur_chara;
    int ailment = 0;

    if (!coop::Mutated("chrid")) {
        chara = TargetOf(NowMonstorUnit->current_monster).chara;
    }

    if (UserStatus->ailments[chara] != 0 && kind != 0) {
        ailment = UserStatus->ailment_frames[chara];
    }

    SetStack(stack, ailment);
    return 1;
}
} // namespace dc::game

namespace coop {

int TargetsOnPartner() {
    int n = 0;
    for (int t : g_target) {
        n += t == 1;
    }
    return n;
}

// coop: gaps2 (the monster-script test, under DC_COOP_DEBUG once a second): for each awake monster
// after the partner, what _GET_CHR_ID tells its script.
void TargetOpcodeCheck() {
    for (int i = 0; i < 16; i++) {
        if (NowMonstorUnit->monster[i].state != 2 || g_target[i] != 1) {
            continue;
        }

        RS_STACKDATA answer{};
        RS_STACKDATA slot{};
        int          saved = NowMonstorUnit->current_monster;

        answer.type = RS_INT;
        slot.type = RS_PTR;
        slot.p = &answer;
        NowMonstorUnit->current_monster = i;
        _GET_CHR_ID(&slot, 1);
        NowMonstorUnit->current_monster = saved;
        std::printf("coop-debug: monster %d after p2 reads chr id %d (p2 member %d, p1 member %d)\n", i, answer.i, g_partner.loaded_chara,
                    (int) UserStatus->cur_chara);
    }
}

} // namespace coop
