// coop-p2absent: motionDrive as ps2/src/dun/gameloop.cpp has it, but for its regeneration counter. The
// PS2 keeps it in a function static (a weapon with flag 0x800 heals 1 HP every 240 ticks); here it is
// MotionDriveHealCounter, which the co-op link zeroes on both copies at a resync and holds in its
// checksum (netplay.cpp). Each copy had otherwise counted its own ticks before a join, and the +1 HP
// came on different ticks (Toan's life 41 against 43). Everything else is the decompiled code.
#include "coop/game_ns.hpp"
#include "dun/gameloop.hpp"
#include "dun/motiondrive.hpp"
#include "coop/coop_combat.hpp" // coop: combat (the player lying down)
#include "coop/coop_core.hpp"
#include "coop/coop_pause.hpp" // coop-chestworld
#include <libvu0.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "battlemenu.hpp"
#include "btactstatus.hpp"
#include "btitem.hpp"
#include "btmisc.hpp"
#include "btsysscript.hpp"
#include "camera.hpp"
#include "camerafollow.hpp"
#include "character.hpp"
#include "clothread.hpp"
#include "clsmes.hpp"
#include "collision.hpp"
#include "collisiondata.hpp"
#include "dataalloc.hpp"
#include "dataread.hpp"
#include "dataset.hpp"
#include "dispctrl.hpp"
#include "dngmessageman.hpp"
#include "dngstatusdata.hpp"
#include "dranmapfield.hpp"
#include "dungeoneventman.hpp"
#include "dungeonmap.hpp"
#include "edit.hpp"
#include "editloop3.hpp"
#include "frame.hpp"
#include "frameattr.hpp"
#include "gamemode.hpp"
#include "gamepad.hpp"
#include "gameutil.hpp"
#include "healeffect.hpp"
#include "hit_machingun_effect.hpp"
#include "hitmark.hpp"
#include "hitvalue.hpp"
#include "itembombeffect.hpp"
#include "itemdata.hpp"
#include "main.hpp"
#include "mainitemmodel.hpp"
#include "mainselect.hpp"
#include "mathutil.hpp"
#include "mds.hpp"
#include "menu_draw.hpp"
#include "menu_dungeon.hpp"
#include "menu_misc.hpp"
#include "menu_save.hpp"
#include "menuitemstep.hpp"
#include "mglib.hpp"
#include "monstorunit.hpp"
#include "motionmodel.hpp"
#include "nowload.hpp"
#include "npcharacter.hpp"
#include "objanime.hpp"
#include "randomitem.hpp"
#include "rect.hpp"
#include "runeffect.hpp"
#include "runscript_opcodes.hpp"
#include "savedata.hpp"
#include "shot_effect.hpp"
#include "shot_effect_pack.hpp"
#include "shot_firebar.hpp"
#include "shot_freefuncs.hpp"
#include "snd.hpp"
#include "stealitem.hpp"
#include "texture.hpp"
#include "textureanime.hpp"
#include "userstatus.hpp"
#include "vutext.hpp"
#include "weaponeffect.hpp"
#include "weaponelement.hpp"
#include "wind.hpp"

int MotionDriveHealCounter = 0;

// Defined in ps2/src/dun/gameloop.cpp beside motionDrive, declared in no header.
namespace dc::game {
extern CFrameVu1 *CharaFrame;
extern CCharacter CharaHand;
extern CHitMark   WeaponCrashEffect;
extern const std::array<s32, 6> defWeapon__6;
extern BT_ACT_STATUS BtActStatus;
extern s32 BtAllDrawFlag;
extern s32 BtEventMode;
extern CItemBombEffect CBomb__2[3];
extern s32 CEffectHold;
extern s32 CMonUnitHold;
extern CWeaponElement CWeaponElFx[4];
extern CWeaponEffect CWeaponFx;
} // namespace dc::game
extern "C" CCharacter CharaMain;
namespace dc::game {
extern s32 CharaMainHandViewFlag;
extern ClsMes DngMesStb;
} // namespace dc::game
extern "C" CDngMessageMan DngMessMan;
namespace dc::game {
extern CCharacter EscapeEffect;
extern s32 EscapeFlag;
extern CHealEffect HealEffect;
} // namespace dc::game
extern "C" CHitMark HitMark[16];
extern "C" CHitPointMark HitPointMark[16];
namespace dc::game {
extern CHitValue HitValue[32];
extern CSHOT_EFFECT MasekiEffect[5];
extern CHitPointMark MyHitPointMark[16];
extern CNPCharacter NPCUnit[6];
extern CCharacter NewChangeFx;
extern s32 NewChangeFxFlag;
extern CCameraFollow * NowCamera__3;
extern CCollisionData * NowColData;
extern CDungeonMap * NowDngMap;
extern CDranMapField * NowDranMapField;
extern CSHOT_EFFECT * NowMainEffect;
extern CMonstorUnit * NowMonstorUnit;
extern CShockWave * NowShockWave;
extern CSHOT * NowShotData;
extern CSHOT_EFFECT_PACK * NowShotEffect;
extern CCharacter * NowWeapon;
extern WEAPON_HAVE * NowWeaponHave;
extern CSHOT_FIREBAR OzumondFire;
extern CSHOT_MACHINGUN OzumondShot;
} // namespace dc::game
extern "C" CHIT_MACHINGUN_EFFECT OzumondShotEffect;
namespace dc::game {
extern CRandomItem * RandomItem;
} // namespace dc::game
extern "C" CStealItem StealItem;
namespace dc::game {
extern CCharacter Trap_Circle;
extern CUserStatus * UserStatus;
extern CCharacter Water_Splash;
extern s32 Water_Splash_actFlag;
extern s32 atraGetStatus;
extern s32 atraShortGetType;
extern s32 driveNoInterpolate;
extern s32 driveStepHold;
extern CMotionModel itemOpenBig;
extern s32 itemOpenBigFlag;
extern CMotionModel itemOpenBigFx;
extern CMotionModel itemOpenSmall;
extern s32 itemOpenSmallFlag;
extern CMotionModel itemOpenSmallFx;
extern s32 lockOnTargetFlag;
extern CMainItemModel mainItemModel;
extern CCharacter shortAtraEffect;
} // namespace dc::game

namespace {

/**
 * How tall one character stands, and how wide their collision reaches (gameloop.cpp's CharaHeight).
 */
inline float CharaHeight(CUserStatus *status) {
    float chara_height[6] = {16.0f, 14.0f, 16.0f, 16.0f, 18.0f, 15.0f};

    return chara_height[status->cur_chara];
}

} // namespace

namespace dc::game { // repin: was PORT_OVERRIDE; a recipe removes the game's own definition
void motionDrive() {
    sceVu0FVECTOR pos;
    sceVu0FVECTOR rotation;
    sceVu0FVECTOR hand;
    int           chara = UserStatus->cur_chara;
    int           i;

    coop::PresentationWorldRuns(); // coop-chestworld: his chest or Atla holds nothing while she is on the floor

    if (BtAllDrawFlag == 0) {
        return;
    }

    if (BtEventMode == 0) {
        NowMonstorUnit->Step(driveStepHold | CMonUnitHold);
    }

    if (BtActStatus.player_visible != 0 && EdEventInfo.player_draw != 0) {
        sceVu0CopyVector(pos, CharaFrame->position);
        CharaFrame->GetRotation(rotation);
        CharaMain.SetPosition(pos);
        CharaMain.SetRotation(rotation[0], rotation[1], rotation[2]);

        int blend = 0;

        if (driveNoInterpolate != 0) {
            blend = 4;
            driveNoInterpolate = 0;
        }

        // A held step, an event or a status ailment restarts the motion
        // outright; otherwise it only changes where it differs.
        // coop: combat: a player lying down (coop::PlayerLiesDown) keeps movement_locked set, but its fall
        // plays rather than stopping on its first frame (flag 1 every tick) (Jep's 2026-10-24 playtest: no fall when Toan went down).
        bool down = coop::PlayerLiesDown() && !coop::Mutated("p1nofall");

        if (driveStepHold != 0 || EdEventInfo.player_stop != 0 || (BtActStatus.movement_locked != 0 && !down)) {
            CharaMain.motion_no = BtActStatus.motion_no;
            CharaMain.motion_flags = 1;
            CharaMain.motion_speed = -1.0f;

            if (chara == CHARA_XIAO) {
                CCharacter *weapon = NowWeapon;

                weapon->motion_no = BtActStatus.motion_no;
                weapon->motion_flags = 1;
                weapon->motion_speed = -1.0f;
            }

            BtActStatus.main_motion = -1;
        } else if (BtActStatus.main_motion != BtActStatus.motion_no) {
            CharaMain.motion_no = BtActStatus.motion_no;
            CharaMain.motion_flags = blend;
            CharaMain.motion_speed = -1.0f;

            if (chara == CHARA_XIAO) {
                CCharacter *weapon = NowWeapon;

                weapon->motion_no = BtActStatus.motion_no;
                weapon->motion_flags = blend;
                weapon->motion_speed = -1.0f;
            }

            BtActStatus.main_motion = BtActStatus.motion_no;
        }

        // coop: combat: the fall plays once and holds its last frame (motion flag 2), not round again.
        if (down) {
            CharaMain.motion_flags |= 2;
        }

        // Osmond's tail keeps turning on its own.
        if (UserStatus->cur_chara == CHARA_OSMOND) {
            CFrame *tail = CharaMain.frame->SearchFrame("tukene1");
            CFrame *tail_shadow = CharaMain.shadow_frame->SearchFrame("obj46");

            if (tail != NULL) {
                static float y = 0.0f;

                y += 0.35699910f;

                if (!(y < PI_SHORT)) {
                    y = y - TWO_PI_SHORT;
                }

                tail->SetRotType(2);
                tail->SetRotation(0.0f, AngleLimit(y), 0.0f);

                if (tail_shadow != NULL) {
                    tail_shadow->SetRotType(2);
                    tail_shadow->SetRotation(0.0f, AngleLimit(y), 0.0f);
                }
            }
        }

        if (lockOnTargetFlag != 0 && UserStatus->cur_chara == CHARA_XIAO && CharaMainHandViewFlag == 0) {
            CharaMain.motion_type.state.look_frame = (CFrameVu1 *) CharaFrame->SearchFrame("jnt2_1");
            sceVu0CopyVector(CharaMain.motion_type.state.look_position, BtActStatus.target_position);
            CharaMain.motion_type.state.look_at = true;
            CharaMain.motion_type.state.look_constraint = FRAME_CONSTRAINT_X;
        } else {
            CharaMain.motion_type.state.look_at = false;
        }

        CharaMain.Step();

        if (chara == CHARA_XIAO) {
            NowWeapon->Step();
        }

        if (BtActStatus.shadow_visible != 0) {
            CharaMain.ShadowStep();
        }
    }

    if ((CharaMainHandViewFlag != 0 && UserStatus->cur_chara == CHARA_XIAO) || UserStatus->cur_chara == CHARA_RUBY) {
        sceVu0CopyVector(hand, CharaFrame->position);

        float height = CharaHeight(UserStatus);

        hand[1] += height - 3.0f;
        CharaHand.SetPosition(hand);

        int blend = 0;

        if (driveNoInterpolate != 0) {
            blend = 4;
            driveNoInterpolate = 0;
        }

        if (driveStepHold != 0 || BtActStatus.movement_locked != 0) {
            CharaHand.motion_no = BtActStatus.motion_no;
            CharaHand.motion_flags = 1;
            CharaHand.motion_speed = -1.0f;

            if (chara == CHARA_XIAO) {
                CCharacter *weapon = NowWeapon;

                weapon->motion_no = BtActStatus.motion_no;
                weapon->motion_flags = 1;
                weapon->motion_speed = -1.0f;
            }

            BtActStatus.hand_motion = -1;
        } else if (BtActStatus.hand_motion != BtActStatus.motion_no) {
            CharaHand.motion_no = BtActStatus.motion_no;
            CharaHand.motion_flags = blend;
            CharaHand.motion_speed = -1.0f;

            if (chara == CHARA_XIAO) {
                CCharacter *weapon = NowWeapon;

                weapon->motion_no = BtActStatus.motion_no;
                weapon->motion_flags = blend;
                weapon->motion_speed = -1.0f;
            }

            BtActStatus.hand_motion = BtActStatus.motion_no;
        }

        if (chara == CHARA_XIAO) {
            NowWeapon->Step();
        }

        CharaHand.Step();
    }

    if (BtEventMode != 0) {
        for (int i = 0; i < 6; i++) {
            if (EdEventInfo.npc_stop[i] == 0) {
                NPCUnit[i].ShadowStep();
                NPCUnit[i].Step();
            }
        }
    }

    if (driveStepHold == 1) {
        return;
    }

    if (BtActStatus.event_block_frames > 0) {
        BtActStatus.event_block_frames--;
    }

    // A weapon that is nearly broken keeps saying so.
    if (BtEventMode == 0) {
        WEAPON_HAVE *weapon;
        CUserStatus *status;
        s8          *slots;
        s8          *check_slots;
        s8           owner;
        s8           check_owner;

        status = UserStatus;
        owner = check_owner = status->cur_chara;
        slots = status->equipped_weapon_slot;
        check_slots = status->equipped_weapon_slot;
        weapon = &status->chara_weapons[owner][(s8) slots[owner]];

        if (weapon->durability_f <= 10.0f && status->chara_weapons[check_owner][(s8) check_slots[check_owner]].item_no != defWeapon__6[(s8) owner]) {
            static int  warning_cnt;
            static char init;
            int         wait;

            if (init == 0) {
                warning_cnt = 0;
                init = 1;
            }

            wait = (int) weapon->durability_f * 10;

            if (wait < 11) {
                wait = 10;
            }

            if (warning_cnt <= 0) {
                warning_cnt = wait;
                SndSePlay(SE_WARNING, -1, 0);
            } else {
                warning_cnt--;
            }
        }
    }

    if (BtActStatus.player_visible != 0 && EdEventInfo.player_draw != 0) {
        CharaMain.ClothStep(0);
    }

    if (BtActStatus.camera_hold == 0) {
        NowCamera__3->Step(1);
    }

    if (Water_Splash_actFlag != 0) {
        Water_Splash.Step();

        float end = (float) Water_Splash.motion_type.motion_info[0].end;

        if (Water_Splash.motion_type.state.time >= end - 0.5f && Water_Splash.motion_type.state.time < end) {
            Water_Splash_actFlag = 0;
        }
    }

    StepWaterLing();
    NowDngMap->StepTrapCircle();
    Trap_Circle.Step();

    if (EscapeFlag != 0) {
        EscapeEffect.Step();
    }

    ItemVolumeStep.LoopStep(60);
    NowDngMap->StepNPC();
    NowDranMapField->CDranMapField::Step();
    DngMesStb.Step();

    if (DngMessMan.timer > 0) {
        DngMessMan.timer--;

        if (DngMessMan.timer == 0) {
            DngMessMan.steev_window = false;
            DngMessMan.message = -1;
        }
    }

    if (DngMessMan.hide_timer > 0) {
        DngMessMan.hide_timer--;
    } else {
        DngMessMan.hide_timer = 0;
    }

    // Every hit that is still in the air counts down and drifts.
    CCollisionData *collision = NowColData;

    for (i = 0; i < 96; i++) {
        if (collision->active[i] == 0) {
            continue;
        }

        if (collision->hit[i].phase > 0) {
            collision->hit[i].phase--;
        } else {
            collision->hit[i].phase = collision->hit[i].ready_phase;
        }

        collision->hit[i].life--;

        if (collision->hit[i].life <= 0) {
            collision->active[i] = 0;
            collision->hit[i].scale_rate = 0.0f;
            continue;
        }

        collision->hit[i].radius += collision->hit[i].unk_40;

        if (collision->hit[i].scale_rate != 0.0f) {
            sceVu0ScaleVectorXYZ(collision->hit[i].base_point, collision->hit[i].base_point, collision->hit[i].scale_rate);
        }
    }

    if (StatusErrCheck(AILMENT_FREEZE) != 0 || StatusErrCheck(AILMENT_STAMINA) != 0) {
        CUserStatus *status = UserStatus;
        s8           owner = status->cur_chara;
        s16         *left = status->ailment_frames;

        if (left[status->cur_chara] > 0) {
            left[owner]--;

            CUserStatus *now = UserStatus;
            s8           who = now->cur_chara;

            if (now->ailment_frames[now->cur_chara] == 0) {
                now->ailments[who] = 0;
                BtActStatus.movement_locked = 0;
            }
        }
    }

    if (BtActStatus.special_cooldown > 0) {
        BtActStatus.special_cooldown--;
    }

    // The action gauge fills back up at a rate the weapon sets.
    if ((int) BtActStatus.action_gauge < 100) {
        if (BtActStatus.refill_frames < 10) {
            BtActStatus.refill_frames++;
        }

        float gain = (float) NowWeaponHave->speed / 30.0f;

        if (StatusErrCheck(AILMENT_GOO) != 0) {
            gain /= 2.0f;
        }

        if (StatusErrCheck(AILMENT_STAMINA) != 0) {
            gain *= 2.0f;
        }

        if (gain < 1.0f) {
            gain = 1.0f;
        }

        CUserStatus *status = UserStatus;
        s8          *who_ref = &status->cur_chara;

        if (status->cur_chara == CHARA_TOAN) {
            if (BtActStatus.swinging == 0) {
                gain *= 0.25f;
            } else {
                gain = 0.0f;
            }

            BtActStatus.swinging = 0;
        }

        if (*who_ref == CHARA_UNGAGA) {
            if (BtActStatus.swinging == 0) {
                gain *= 0.25f;
            } else {
                gain = 0.0f;
            }

            BtActStatus.swinging = 0;
        }

        s8 who = *who_ref;
        s8 who_copy = *who_ref;

        if (*who_ref == CHARA_OSMOND) {
            if (BtActStatus.gun_type != 1) {
                if (BtActStatus.gauge_exhausted == 0) {
                    gain = 0.0f;
                } else {
                    gain *= 0.5f;
                }
            } else {
                gain *= 1.5f;
            }
        }

        if (who == CHARA_XIAO) {
            gain *= 1.5f;
        }

        if (who_copy == CHARA_RUBY) {
            gain *= 1.5f;
        }

        BtActStatus.action_gauge += gain;

        if (!((int) BtActStatus.action_gauge < 100)) {
            BtActStatus.action_gauge = 100.0f;
            BtActStatus.gauge_flash = 10;
            BtActStatus.gauge_exhausted = 0;
        }
    } else if (BtActStatus.gauge_flash > 0) {
        BtActStatus.gauge_flash--;
    }

    if (BtActStatus.hud_shake_frames > 0) {
        BtActStatus.hud_shake_y = (int) (16.0f * (float) rand() / 2147483648.0f) - 8;
        BtActStatus.hud_shake_frames--;
    }

    BtActStatus.frames_since_attack++;

    if (!(BtActStatus.frames_since_attack < 3600)) {
        BtActStatus.frames_since_attack = 3600;
    }

    if (UserStatus->step_disable == 0) {
        BtStatusErrStep();
    }

    CUserStatus *status = UserStatus;
    s8          *healed_ref = &status->cur_chara;
    float        water_max = status->water_max[*healed_ref];
    float        water_now = status->water_now[*healed_ref];
    int         &heal_counter = MotionDriveHealCounter; // coop-p2absent

    heal_counter++;

    if (!(heal_counter < 240)) {
        if ((NowWeaponHave->flags & 0x800) && status->hp[*healed_ref] > 0) {
            status->AddNowLife((s8) *healed_ref, 1, 255.0f);
        }

        heal_counter = 0;
    }

    UserStatus->Step(0);
    float dry = UserStatus->water_now[UserStatus->cur_chara];

    DngMessMan.SetStatus_Dry(water_max, water_now, dry);
    BtStatusAlarmAnime();

    for (int i = 0; i < FrameObjAnimCnt; i++) {
        ObjAnimePlay(&FrameObjAnim[i]);
    }

    CDungeonMap *map = NowDngMap;

    map->fire.FireStep();
    map->fire.RasterStep();
    CWeaponFx.Step();

    for (int i = 0; i < 4; i++) {
        CWeaponElFx[i].Step();
    }

    for (int i = 0; i < 32; i++) {
        HitValue[i].Step();
    }

    mainItemModel.Step();
    WeaponCrashEffect.Step();

    for (int i = 0; i < 16; i++) {
        HitMark[i].Step();
        HitPointMark[i].Step();
        MyHitPointMark[i].Step();
    }

    if (NewChangeFxFlag != 0) {
        NewChangeFx.Step();

        float end = (float) NewChangeFx.motion_type.motion_info[0].end;

        if (NewChangeFx.motion_type.state.time >= end - 0.3f && NewChangeFx.motion_type.state.time < end) {
            NewChangeFxFlag = 0;
        }
    }

    if (atraShortGetType != 0 && atraGetStatus != 0) {
        shortAtraEffect.Step();
    }

    if (itemOpenSmallFlag == 1) {
        itemOpenSmall.Step();
        itemOpenSmallFx.Step();
    }

    if (itemOpenBigFlag == 1) {
        itemOpenBig.Step();
        itemOpenBigFx.Step();
    }

    if (CEffectHold != 0 || BtEventMode != 0) {
        return;
    }

    if (BtActStatus.camera_shake_frames > 0) {
        float shake = BtActStatus.camera_shake;

        BtActStatus.camera_shake_offset = 2.0f * (shake * (float) rand()) / 2147483648.0f - shake;
        BtActStatus.camera_shake -= BtActStatus.camera_shake_decay;
        BtActStatus.camera_shake_frames--;
    } else {
        BtActStatus.camera_shake_offset = 0.0f;
    }

    if (BtActStatus.fast_camera_frames > 0) {
        BtActStatus.fast_camera_frames--;
    }

    int          picked_up = RandomItem->CheckPosition();
    CUserStatus *collector = UserStatus;
    int          total = collector->money + picked_up;

    if (!(total < 0xFFFF)) {
        collector->money = 0xFFFF;
    } else {
        collector->money = total;
    }

    RandomItem->Step();
    StealItem.Step();

    for (int i = 0; i < 3; i++) {
        CBomb__2[i].Step();
    }

    for (int i = 0; i < 5; i++) {
        MasekiEffect[i].Step();
    }

    NowShockWave->Step();

    CSHOT_EFFECT *effects = NowShotEffect->effect;

    for (i = 0; i < 5; i++) {
        effects[i].Step();
    }

    OzumondShot.Step();
    OzumondFire.Step();
    OzumondShotEffect.Step();
    NowMainEffect->Step();
    NowShotData->step();
    HealEffect.Step();
}
} // namespace dc::game
