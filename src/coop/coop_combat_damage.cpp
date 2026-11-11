// CMonstorUnit::CheckDmg (ps2/src/monstorunit.cpp) for co-op: retail's, with the partner's hits (owner
// its character, not the controlled one's; MarkHit) taken by the game's own rules for that character,
// but wearing the partner's weapon rather than the controlled character's, and a partner's shot falling
// off with its distance from the partner rather than from the player. Without co-op, or for any other
// hit, it is retail's. The three changes are marked "coop: combat".

#include "coop/game_ns.hpp"
#include "monstorunit.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "btactstatus.hpp"
#include "collisiondata.hpp"
#include "dataalloc.hpp"
#include "dataread.hpp"
#include "dngmessageman.hpp"
#include "dngstatusdata.hpp"
#include "dun/gameloop.hpp"
#include "dungeonmap.hpp"
#include "dungeonparts.hpp"
#include "frame.hpp"
#include "framevu1.hpp"
#include "gameutil.hpp"
#include "hitmark.hpp"
#include "hitvalue.hpp"
#include "itemdata.hpp"
#include "mathutil.hpp"
#include "menu_inventory.hpp"
#include "mglib.hpp"
#include "nowload.hpp"
#include "randomitem.hpp"
#include "rect.hpp"
#include "runscript_opcodes.hpp"
#include "savedata.hpp"
#include "shot_effect_pack.hpp"
#include "snd.hpp"
#include "coop/coop_audio.hpp" // coop-soundpt
#include "stealitem.hpp"
#include "texture.hpp"
#include "userstatus.hpp"
#include "weaponelement.hpp"

#include "coop/coop_inventory.hpp" // coop-inventory
#include "coop/coop.hpp"
#include "coop/coop_combat.hpp"
#include "coop/coop_core.hpp"

extern "C" int hitCnt;

// coop: netplay: the weapon element spark slot the next elemental hit lights (retail's function static), at file
// scope so a resync can start it over on both copies (PortWeaponFxSlotReset): a copy that has fought a while has it
// elsewhere than one that has not, and the slots' sparks roll rand on their own timers.
static int g_weapon_fx_next = 0;

void PortWeaponFxSlotReset() {
    g_weapon_fx_next = 0;
}

// coop-kocatchup: the slot for the checksum (a snapshot carries it: build\snapshot-allow.txt).
int PortWeaponFxSlot() {
    return g_weapon_fx_next;
}

// A test's way to leave the slot where a long session might (DC_NETPLAY_PERTURB_WEAPONFX).
void PortWeaponFxSlotSet(int slot) {
    g_weapon_fx_next = slot & 3;
}

namespace dc::game { // repin: was PORT_OVERRIDE; a recipe removes the game's own definition
MonsterDamageResult CMonstorUnit::CheckDmg() {
    MonsterDamageResult result = MonsterDamageResult::None;
    COLLISION_HIT *record;
    int            immune = 0;
    sceVu0FVECTOR  direction, position;

    if (monster[current_monster].stop_timer <= 0) {
        for (int i = 0; i < 16; i++) {
            if (effect2[current_monster].active[i] != 0) {
                float time = chara[current_monster][0].motion_type.state.time;

                if (effect2[current_monster].motion_start[i] < time && !(effect2[current_monster].motion_end[i] <= time)) {
                    int damage = effect2[current_monster].damage[i];

                    if (monster[current_monster].anger_timer > 0) {
                        damage *= 2;
                    }

                    int hit = NowColData->Set(effect2[current_monster].position[i], damage, 2, effect2[current_monster].radius[i], 0.0f, 1, effect2[current_monster].kind[i], effect2[current_monster].flags[i], 0);
                    NowColData->SetUserID(current_monster * 5 + 200, i);

                    if (effect2[current_monster].kind[i] == 3) {
                        float angle = effect2[current_monster].angle[i];

                        if (angle == 0.0f) {
                            sceVu0CopyVector(direction, CharaMain.pos);
                            chara[current_monster][0].GetPosition(position);
                            direction[0] -= position[0];
                            direction[1] = 0;
                            direction[2] -= position[2];
                            direction[3] = 1;
                            sceVu0Normalize(direction, direction);
                            NowColData->SetVelocity(hit, direction, 1.0f);
                        } else {
                            if (!(angle < 180.0f)) {
                                angle -= 360.0f;
                            }

                            angle = DEG_TO_RAD * angle;
                            angle += chara[current_monster][0].GetRotation()->y;

                            if (!(angle <= TWO_PI)) {
                                angle -= TWO_PI;
                            }

                            if (angle < -PI) {
                                angle += TWO_PI;
                            }

                            sceVu0FVECTOR forward = {0, 0, 1, 0};
                            sceVu0FMATRIX rotation_matrix, identity_matrix;
                            sceVu0UnitMatrix(identity_matrix);
                            sceVu0RotMatrixY(rotation_matrix, identity_matrix, angle);
                            sceVu0ApplyMatrix(forward, rotation_matrix, forward);
                            NowColData->SetVelocity(hit, forward, 1.0f);
                        }
                    }
                }
            }
        }
    }

    sceVu0FVECTOR poison_position, guard_position, guard_origin, guard_direction;
    sceVu0FVECTOR steal_position, knockback_position, knockback_origin, hit_direction, player_position;

    if (monster[current_monster].hp <= 0) {
        if (monster[current_monster].stop_timer > 0) {
            monster[current_monster].stop_timer = 0;
            monster[current_monster].motion_reset_pending = true;
        }

        return MonsterDamageResult::None;
    }

    monster[current_monster].last_hit_id = -1;

    if (monster[current_monster].stop_timer > 0) {
        monster[current_monster].palette_override[0] = 160.0f;
        monster[current_monster].palette_override[1] = 160.0f;
        monster[current_monster].palette_override[2] = 160.0f;
        monster[current_monster].palette_override_pending = true;
        monster[current_monster].stop_timer--;

        if (monster[current_monster].stop_timer == 0) {
            monster[current_monster].motion_reset_pending = true;
        }
    }

    if (monster[current_monster].slow_timer > 0) {
        monster[current_monster].palette_override[0] = 25.0f;
        monster[current_monster].palette_override[1] = 37.5f;
        monster[current_monster].palette_override[2] = 63.75f;
        monster[current_monster].palette_override_pending = true;
    }

    if (monster[current_monster].anger_timer > 0) {
        monster[current_monster].palette_override[0] = 127.5f;
        monster[current_monster].palette_override[1] = 80.0f;
        monster[current_monster].palette_override[2] = 15.0f;
        monster[current_monster].palette_override_pending = true;
        monster[current_monster].anger_timer--;
    }

    if (monster[current_monster].poison_timer > 0) {
        monster[current_monster].palette_override[0] = 47.0f;
        monster[current_monster].palette_override[1] = 0.5f;
        monster[current_monster].palette_override[2] = 63.75f;
        monster[current_monster].palette_override_pending = true;
        monster[current_monster].poison_timer--;

        if (monster[current_monster].poison_timer == 0) {
            monster[current_monster].poison_timer = 180;
            float damage = 0.1f * (float) monster[current_monster].max_hp;
            monster[current_monster].hp -= (int) damage;
            result = MonsterDamageResult::Hit;

            if (monster[current_monster].hp <= 0) {
                monster[current_monster].hp = 0;
                alive_count--;
                result = MonsterDamageResult::Killed;
                ((CDngStatusData *) UserStatus)->AddKills();
            }

            chara[current_monster][0].GetPosition(poison_position);
            poison_position[1] += chara[current_monster][0].body_height;
            HitValueEntry(NowHitValue, poison_position, (int) damage, HIT_VALUE_MONSTER, NULL);
        }
    }

    if (monster[current_monster].invincible_timer > 0) {
        return result;
    }

    monster[current_monster].invincible_blocked = 0;

    for (int i = 0; i < 16; i++) {
        if (effect[current_monster].timer[i] != 0) {
            int   active = true;
            float incoming_time;

            if (effect[current_monster].motion_start[i] != 0.0f && (!(effect[current_monster].motion_start[i] < (incoming_time = chara[current_monster][0].motion_type.state.time)) || effect[current_monster].motion_end[i] < incoming_time)) {
                active = false;
            }

            if (active != 0) {
                int hit;
                int element;
                int owner;
                hit = NowColData->FindMonsterHit(effect[current_monster].position[i], effect[current_monster].radius[i]);
                int rejected = 0;

                if (hit != -1) {
                    int monster_owner = NowColData->GetMonsterOwner(hit);

                    if (monster_owner == current_monster) {
                        rejected = 1;
                    }

                    if (monster_owner != -1 && monster_owner != current_monster) {
                        COLLISION_HIT *other_record = &(*NowColData->Get(hit));
                        float          chance = 100.0f * (float) rand() / 2147483648.0f;

                        if (other_record->flags & 0x1000) {
                            if (chance < (float) monster[current_monster].status_chance && monster[current_monster].anger_timer == 0) {
                                monster[current_monster].anger_timer = 1800;
                                monster[current_monster].poison_timer = 0;
                                monster[current_monster].slow_timer = 0;
                                SndSePlay(SE_POWER_UP, -1, 0);
                            }
                        }

                        rejected = 1;
                    }
                }

                if (hit != -1 && rejected == 0) {
                    for (int j = 0; j < 3; j++) {
                        if (guard[current_monster].active[j] != 0) {
                            float time = chara[current_monster][0].motion_type.state.time;

                            if (guard[current_monster].motion_start[j] <= time && !(guard[current_monster].motion_end[j] < time)) {
                                record = &(*NowColData->Get(hit));

                                if (record->knockback_mode == 2) {
                                    chara[current_monster][0].GetPosition(guard_position);
                                    sceVu0CopyVector(guard_origin, record->knockback_origin);
                                    guard_position[1] = 0;
                                    guard_origin[1] = 0;
                                    sceVu0SubVector(monster[current_monster].knockback_direction, guard_position, guard_origin);
                                    sceVu0Normalize(monster[current_monster].knockback_direction, monster[current_monster].knockback_direction);
                                    monster[current_monster].knockback[0] = 1.5f * record->knockback_speed * monster[current_monster].knockback[2];
                                    monster[current_monster].knockback[1] = 1.5f * record->knockback_decay * monster[current_monster].knockback[2];
                                }

                                int owner = NowColData->hit[hit].owner;

                                if (NowColData->hit[hit].attack_no == 0 && (owner == 0 || owner == 2 || owner == 4)) {
                                    // coop: combat (the partner's hit wears the partner's weapon)
                                    if (coop::PartnerHit(owner)) {
                                        coop::PartnerWeaponWear(0.1f, monster[current_monster].hardness);
                                    } else {
                                        SwordDmgCheck1(0.1f, monster[current_monster].hardness);
                                    }
                                }

                                guard_direction[0] = 0;
                                guard_direction[1] = 2.5f;
                                guard_direction[2] = 0;
                                guard_direction[3] = 1;
                                HitMark[hitCnt].Set(effect[current_monster].position[i], guard_direction, HIT_MARK_GUARD, 0.8f, 0.005f, 0.02f, 1.3f, 32, monster[current_monster].ground_y);
                                HitPointMark[hitCnt].Set(effect[current_monster].position[i]);
                                SndSePlay(SE_HIT_BLOCKED, -1, 0);
                                rejected = 1;
                                j = 3;
                            }
                        }
                    }
                }

                int hit_id = -1;

                if (hit != -1 && rejected == 0) {
                    COLLISION_HIT *original_record = &(*NowColData->Get(hit));
                    effect[current_monster].hit_slot = i;
                    int attack = NowColData->hit[hit].attack_no;
                    owner = NowColData->GetUserID(hit);

                    if (owner != -1) {
                        hit_id = owner * 10;
                    }

                    if (attack != -1) {
                        hit_id += attack;
                    }

                    monster[current_monster].last_hit_id = hit_id;
                    effect[current_monster].hit_attributes = NowColData->hit[hit].weapon_flags;
                    int element_flags = NowColData->GetFlags(hit);
                    printf("element = %d\n", element_flags);
                    int item;

                    switch (element_flags) {
                        case HIT_ELEMENT_FIRE:
                            element = WEAPON_ELEMENT_FIRE;
                            break;
                        case HIT_ELEMENT_COLD:
                            element = WEAPON_ELEMENT_COLD;
                            break;
                        case HIT_ELEMENT_THUNDER:
                            element = WEAPON_ELEMENT_THUNDER;
                            break;
                        case HIT_ELEMENT_WIND:
                            element = WEAPON_ELEMENT_WIND;
                            break;
                        case HIT_ELEMENT_HOLY:
                            element = WEAPON_ELEMENT_HOLY;
                            break;
                        default:
                            element = WEAPON_ELEMENT_NONE;
                            break;
                    }

                    monster[current_monster].hit_element = element;

                    if (monster[current_monster].steal_item != -1 && (effect[current_monster].hit_attributes & 0x80) && (int) (100.0f * (float) rand() / 2147483648.0f) < 10 && (item = monster[current_monster].steal_item, coop::StealRoomCheck(NowColData->hit[hit].owner, item)) == 0) { // coop-inventory: P2's bag for P2's hits
                        chara[current_monster][0].GetPosition(steal_position);
                        steal_position[1] += 12.0f;
                        coop::StealSet(steal_position, monster[current_monster].steal_item, NowColData->hit[hit].owner); // coop-inventory: records P2's steal
                        monster[current_monster].steal_item = -1;
                    }

                    int attacker = NowColData->hit[hit].owner;

                    if (attacker == 0 || attacker == 2 || attacker == 4) {
                        // coop: combat (the partner's hit wears the partner's weapon)
                        if (coop::PartnerHit(attacker)) {
                            coop::PartnerWeaponWear(1.0f, monster[current_monster].hardness);
                        } else {
                            SwordDmgCheck1(1.0f, monster[current_monster].hardness);
                        }
                    }

                    result = MonsterDamageResult::Hit;
                    int boss = 0;

                    if (selectMapNo == DUNGEON_DIVINE_BEAST_CAVE && UserStatus->cur_floor == 14) {
                        boss = 1;
                    }

                    if (selectMapNo == DUNGEON_GALLERY_OF_TIME && UserStatus->cur_floor == 24) {
                        boss = 1;
                    }

                    if (boss == 0) {
                        if (owner == 5 && NowColData->hit[hit].attack_no == 6) {
                            result = MonsterDamageResult::None;
                        }

                        if (owner == 1) {
                            result = MonsterDamageResult::None;
                        }
                    }

                    record = &(*NowColData->Get(hit));

                    if (record->knockback_mode == 2) {
                        chara[current_monster][0].GetPosition(knockback_position);
                        sceVu0CopyVector(knockback_origin, record->knockback_origin);
                        knockback_position[1] = 0;
                        knockback_origin[1] = 0;
                        sceVu0SubVector(monster[current_monster].knockback_direction, knockback_position, knockback_origin);
                        sceVu0Normalize(monster[current_monster].knockback_direction, monster[current_monster].knockback_direction);
                        monster[current_monster].knockback[0] = record->knockback_speed * monster[current_monster].knockback[2];
                        monster[current_monster].knockback[1] = record->knockback_decay * monster[current_monster].knockback[2];
                    }

                    hit_direction[0] = 0;
                    hit_direction[1] = 1.1f;
                    hit_direction[2] = 0;
                    hit_direction[3] = 1;
                    const float mark_scale = 1.0f;
                    const float mark_spread = 1.0f / 2.0f;
                    HitMark[hitCnt].Set(effect[current_monster].position[i], hit_direction, HIT_MARK_HIT, mark_spread, 0.01f, 0.02f, mark_scale, 32, monster[current_monster].ground_y);
                    HitPointMark[hitCnt].Set(effect[current_monster].position[i]);

                    if (hitCnt == 15) {
                        hitCnt = 0;
                    } else {
                        hitCnt++;
                    }

                    if (element < WEAPON_ELEMENT_NONE) {
                        CDngStatusData *status = (CDngStatusData *) UserStatus;
                        WEAPON_HAVE    *weapon = &status->chara_weapons[owner][status->equipped_weapon_slot[owner]];
                        float           strength = (float) weapon->elem[weapon->best_elem];
                        int            &cnt = g_weapon_fx_next; // coop: netplay: retail's static, which a resync starts over
                        CWeaponElFx[cnt].Set(&effect[current_monster].position[i], effect[current_monster].position[i], strength, element, monster[current_monster].body_radius);

                        if (cnt >= 3) {
                            cnt = 0;
                        } else {
                            cnt++;
                        }
                    }

                    float chance = 100.0f * (float) rand() / 2147483648.0f;

                    if (effect[current_monster].hit_attributes & 0x20) {
                        if (100.0f * (float) rand() / 2147483648.0f <= 10.0f && chance < (float) monster[current_monster].status_chance) {
                            monster[current_monster].poison_timer = 180;
                            monster[current_monster].slow_timer = 0;
                        } else if (owner == -1) {
                            immune = 1;
                        }
                    }

                    if (effect[current_monster].hit_attributes & 0x40) {
                        if (100.0f * (float) rand() / 2147483648.0f <= 4.0f && chance < (float) monster[current_monster].status_chance) {
                            if (monster[current_monster].stop_timer <= 0) {
                                monster[current_monster].stop_timer = 300;
                                monster[current_monster].poison_timer = 0;
                                monster[current_monster].slow_timer = 0;
                                monster[current_monster].anger_timer = 0;
                                monster[current_monster].movement_speed = 0;
                            } else {
                                monster[current_monster].stop_timer = 0;
                            }
                        } else if (owner == -1) {
                            immune = 1;
                        }
                    }

                    chance = monster[current_monster].status_chance == 0 ? 100.0f : 0.0f;

                    if (original_record->flags & 0x200) {
                        if (chance < (float) monster[current_monster].status_chance) {
                            if (monster[current_monster].stop_timer == 0 && monster[current_monster].anger_timer == 0) {
                                monster[current_monster].poison_timer = 180;
                                monster[current_monster].slow_timer = 0;
                            }
                        } else if (owner == -1) {
                            immune = 1;
                        }
                    }

                    if (original_record->flags & 0x100) {
                        if (chance < (float) monster[current_monster].status_chance) {
                            if (monster[current_monster].stop_timer <= 0) {
                                monster[current_monster].stop_timer = 300;
                                monster[current_monster].poison_timer = 0;
                                monster[current_monster].slow_timer = 0;
                                monster[current_monster].anger_timer = 0;
                                monster[current_monster].movement_speed = 0;
                            } else {
                                monster[current_monster].stop_timer = 0;
                            }
                        } else if (owner == -1) {
                            immune = 1;
                        }
                    }

                    if (original_record->flags & 0x800) {
                        if (chance < (float) monster[current_monster].status_chance) {
                            if (monster[current_monster].stop_timer == 0 && monster[current_monster].poison_timer == 0 && monster[current_monster].anger_timer == 0) {
                                monster[current_monster].slow_timer = 180;
                            }
                        } else if (owner == -1) {
                            immune = 1;
                        }
                    }

                    float damage = (float) record->damage;

                    if (owner == 1 || owner == 3 || owner == 5) {
                        // coop: combat (a partner's shot falls off with the distance from the partner)
                        sceVu0CopyVector(player_position, coop::PartnerHit(owner) ? coop::g_partner.pos : CharaMain.pos);
                        float distance = DistVector(player_position, record->pos);

                        if (distance <= 20.0f) {
                            damage *= 1.5f;
                            printf("c_dist = %.3f\n", 1.5);
                        }

                        if (!(distance < 50.0f)) {
                            distance -= 50.0f;
                            distance = (100.0f - distance) / 100.0f;

                            if (distance < 0.5f) {
                                distance = 0.5f;
                            }

                            damage *= distance;
                            printf("c_dist = %.3f\n", distance);
                        }
                    }

                    int   index = GetCurrentMonsterIndex();
                    float defense = (float) monster[index].defense;

                    if (owner == 3) {
                        defense /= 2.0f;
                    }

                    damage -= defense;

                    if (damage <= 0.0f) {
                        damage = 1.0f;
                    }

                    if (element_flags != 0) {
                        float old_damage = damage;
                        float bonus = 0;

                        if (owner != -1) {
                            WEAPON_HAVE *weapon = &UserStatus->chara_weapons[owner][UserStatus->equipped_weapon_slot[owner]];
                            bonus = damage * (0.005f * (float) weapon->magic + 0.004f * (float) weapon->elem[element]);
                        }

                        damage += bonus;
                        damage *= 0.01f * (float) monster[index].attachment_weight[element];

                        if (damage <= 0.0f && !(old_damage <= 0.0f)) {
                            immune = 1;
                        }
                    }

                    char *effectiveness = record->vs_monster;

                    if (effectiveness != NULL) {
                        damage += damage * (0.015f * (float) effectiveness[monster[index].attachment_kind]);
                    }

                    if (owner != -1) {
                        float old_damage = damage;
                        int   multiplier = effect[index].parameter[i][owner];
                        float damage_scale = (float) multiplier;
                        damage = damage / 100.0f * damage_scale;

                        if (damage <= 0.0f) {
                            damage = 0;
                        }

                        if (damage <= 0.0f && !(old_damage <= 0.0f)) {
                            immune = 1;
                        }
                    }

                    int target_kind = NowColData->hit[hit].target_kind;

                    if (target_kind != -1 && monster[index].attachment_kind != target_kind) {
                        damage = 0;
                        immune = 1;
                    }

                    if (monster[index].anger_timer > 0) {
                        damage /= 2.0f;
                    }

                    if (owner == -1) {
                        damage *= 0.01f * (float) monster[index].item_damage_rate;
                    }

                    if (damage <= 0.0f) {
                        damage = 0;
                    }

                    int amount = (int) damage;

                    if (!(damage - (float) amount <= 0.0f)) {
                        amount++;
                    }

                    if ((effect[index].hit_attributes & 0x400) && !(damage < 100.0f)) {
                        CUserStatus *status = UserStatus;

                        if (status->hp[(int) owner] > 0) {
                            float heal = 0.01f * damage;
                            int   ignored = (int) heal;
                            status->AddNowLife(owner, (short) (int) heal, 255.0f);
                        }
                    }

                    if ((effect[current_monster].hit_attributes & 0x1000) && monster[current_monster].kind != MONSTER_KIND_NO_LOCK_ON && 100.0f * (float) rand() / 2147483648.0f < 1.0f) {
                        amount = monster[current_monster].hp;
                    }

                    // coop-soundpt: a hit sounds from where it lands, placed against this copy's listener (her hits far from his
                    // camera were heard at full volume on his copy: Jep's playtest, 2026-10-30). DC_COOP_AUDIO_MUTATE=partnerflat: flat.
                    coop::SoundFrom hit_at(effect[current_monster].position[i]);

                    if (amount > 0 && (owner == 0 || owner == 2 || owner == 4) && element >= 0 && element < WEAPON_ELEMENT_NONE) {
                        SndSePlay(element + 0x65, -1, 0);
                    }

                    BtActStatus.monstor_target = owner;
                    monster[current_monster].hp -= amount;

                    if (monster[current_monster].hp <= 0) {
                        monster[current_monster].last_attacker = owner;
                        monster[current_monster].hp = 0;
                        alive_count--;
                        result = MonsterDamageResult::Killed;
                        ((CDngStatusData *) UserStatus)->AddKills();

                        if (monster[current_monster].event_flag2 != -1) {
                            monster[current_monster].event_flag2_pending = 1;
                        }
                    }

                    if (immune == 0) {
                        monster[current_monster].palette_target[0] = 255;
                        monster[current_monster].palette_target[1] = 0;
                        monster[current_monster].palette_target[2] = 0;
                        monster[current_monster].palette_cycles = 1;
                        monster[current_monster].palette_step = 0.08f;
                        monster[current_monster].palette_blend = 0;
                        HitValueEntry(NowHitValue, effect[current_monster].position[i], amount, HIT_VALUE_MONSTER, NULL);
                        SndSePlay(SE_MONSTER_HIT, -1, 0);
                    } else {
                        HitValueEntry(NowHitValue, effect[current_monster].position[i], 0, HIT_VALUE_ZERO, NULL);
                        monster[current_monster].palette_target[0] = 255;
                        monster[current_monster].palette_target[1] = 255;
                        monster[current_monster].palette_target[2] = 255;
                        monster[current_monster].palette_cycles = 1;
                        monster[current_monster].palette_step = 0.08f;
                        monster[current_monster].palette_blend = 0;
                        SndSePlay(SE_HIT_NO_DAMAGE, -1, 0);
                    }

                    if (NowColData->hit[hit].target_mask != 3) {
                        NowColData->active[hit] = 0;
                    }

                    if (result == MonsterDamageResult::Killed) {
                        return MonsterDamageResult::Killed;
                    }

                    break;
                }
            }
        }
    }

    return result;
}
} // namespace dc::game
