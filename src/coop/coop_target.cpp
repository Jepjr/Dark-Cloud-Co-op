#include "coop/game_ns.hpp"
#include <libvu0.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "btactstatus.hpp"
#include "coop/coop.hpp"
#include "coop/coop_core.hpp"
#include "dun/gameloop.hpp"
#include "mathutil.hpp"
#include "monstorunit.hpp"
#include "runscript.hpp"
#include "runscript_opcodes.hpp"
#include "userstatus.hpp"

// The monster script opcodes that read where the player stands, retail's (ps2/src/runscript_opcodes.cpp)
// with CharaMain's position replaced by that of whichever of the two co-op players the monster goes
// after. Without co-op, or with the partner off the floor or down, that is always the player.

namespace {

// Distance the other player has to be nearer by before a monster turns to it, so that one standing
// between the two does not swap every tick.
constexpr float kSwitchMargin = 15.0f;

// Whom each monster goes after: 0 the player, 1 the partner.
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

// The player the current monster goes after: the nearer one, held until the other is nearer by
// kSwitchMargin.
Target TargetOf(int monster_no) {
    Target target;
    float  monster_position[4];

    sceVu0CopyVector(target.position, CharaMain.pos);
    sceVu0CopyVector(target.heading, BtActStatus.input_direction);
    target.since_attack = BtActStatus.frames_since_attack;
    target.chara = UserStatus->cur_chara;
    target.partner = false;

    Target partner = target;

    if (monster_no < 0 || monster_no >= 16 ||
        !CoopPartnerTarget(partner.position, partner.heading, &partner.since_attack, &partner.chara)) {
        if (monster_no >= 0 && monster_no < 16) {
            g_target[monster_no] = 0;
        }

        return target;
    }

    NowMonstorUnit->chara[monster_no][0].GetPosition(monster_position);

    float to_player = DistVector(monster_position, target.position);
    float to_partner = DistVector(monster_position, partner.position);

    if (g_target[monster_no] == 0 && to_partner + kSwitchMargin < to_player) {
        g_target[monster_no] = 1;
    } else if (g_target[monster_no] == 1 && to_player + kSwitchMargin < to_partner) {
        g_target[monster_no] = 0;
    }

    if (g_target[monster_no] == 1) {
        partner.partner = true;
        return partner;
    }

    return target;
}

} // namespace

void CoopTargetReset() {
    for (int &target : g_target) {
        target = 0;
    }
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
