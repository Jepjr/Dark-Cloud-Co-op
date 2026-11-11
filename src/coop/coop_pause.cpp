// Each player's own pause on a dungeon floor; see coop_pause.hpp.

#include "coop/game_ns.hpp"
#include "coop/coop_pause.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

#include "btactstatus.hpp"
#include "text/caption_text.hpp"
#include "character.hpp"
#include "collisiondata.hpp"
#include "dataread.hpp"
#include "dun/gameloop.hpp"
#include "dungeonmap.hpp"
#include "gamepad.hpp"
#include "menu_draw.hpp"
#include "monstorunit.hpp" // coop-fixtures: TestWake
#include "mglib.hpp"
#include "rect.hpp"
#include "snd.hpp"
#include "texture.hpp"
#include "userstatus.hpp"

#include "coop/coop.hpp"
#include "coop/coop_opener.hpp" // coop-opt7: a presentation counts as paused
#include "coop/coop_audio.hpp" // coop-menuse
#include "coop/coop_core.hpp"
#include "coop/coop_fullmenu.hpp"
#include "coop/coop_menu.hpp"
#include "coop/coop_party.hpp"
#include "coop/coop_wheel.hpp" // coop-pausewheel
#include "coop/coop_worlds.hpp" // coop-gatepopup: CoopWorldsQuestionHolds
#include "coop/netplay.hpp"

namespace dc::game {
extern s32 gameTask;
extern int mode;
} // namespace dc::game

namespace coop {

namespace {

constexpr int kDungeonMode = 3; // GAME_MODE_DUNGEON (main.hpp)

struct Pause {
    bool paused[2] = {false, false};
    bool prev_start[2] = {false, false};
    bool held = false; // this module holds the world (driveStepHold and the rest)
} g_pause;

// The Atlas' bobs as they stood before the frame's DrawAtraBoll, and how often it drew the first (the test's).
float g_atla_phase[8] = {};
bool  g_atla_saved = false;
int   g_atla_drawn = 0;

const char *const kNames[] = {"Toan", "Xiao", "Goro", "Ruby", "Ungaga", "Osmond"};

// On a floor under control (the game's menu beside the world counts: its task is play).
bool OnFloor() {
    return PauseOn() && mode == kDungeonMode && gameTask == GAME_TASK_PLAY && BtEventMode == 0 && UserStatus != nullptr;
}

bool Down(int player) {
    return player == 1 ? UserStatus != nullptr && UserStatus->hp[UserStatus->cur_chara] <= 0 : g_partner.down > 0;
}

// Player 2 is in the zone while her character is on this floor.
bool PartnerInZone() {
    return g_partner.visible;
}

bool InMenu(int player) {
    return FullMenuOpenFor(player);
}

void Hold(bool hold) {
    // coop-opt7: while a player presents, the hold is retail's presentation's (monsters and effects): driveStepHold would stand
    // the opener's pose on its first frame each tick.
    // DC_COOP_MUTATE=present_stephold (twin): the whole hold, as a pause's.
    const int step_hold = hold && (!AnyPresenting() || Mutated("present_stephold")) ? 1 : 0;

    if (hold == g_pause.held) {
        if (hold && driveStepHold != step_hold) {
            driveStepHold = step_hold;
        }
        return;
    }
    g_pause.held = hold;
    driveStepHold = step_hold;
    CMonUnitHold = hold ? 1 : 0;
    CEffectHold = hold ? 1 : 0;
    PlayTimeCountFlag(hold ? 0 : 1);
    std::fprintf(stderr, "coop-pause: the world %s (P1 %s, P2 %s)\n", hold ? "holds" : "runs",
                 g_pause.paused[0] ? "paused" : PlayerPresenting(1) ? "in his chest" : InMenu(1) ? "in his menu" : CoopWorldsQuestionHolds(0) ? "in a question" : "playing",
                 !PartnerInZone() ? "not here" : g_pause.paused[1] ? "paused" : PlayerPresenting(2) ? "in her chest" : InMenu(2) ? "in her menu" : CoopWorldsQuestionHolds(1) ? "in a question" : "playing");
}

// Test hook (DC_COOP_PAUSEHIT=P:T:DAMAGE): at T ticks of the menu presses' clock (netplay's test hook), a hit
// of DAMAGE where player P stands, as a scripted hit on the player's side (btsysscript's).
void TestHit() {
    static const char *spec = TestEnv("DC_COOP_PAUSEHIT");
    static const bool  lives_full = TestEnvOn("DC_COOP_TEST_LIVES_FULL");
    int                player = 0, damage = 0;
    long               when = -1;

    // coop-menuflake: DC_COOP_TEST_LIVES_FULL=1 (t-triangle-menu -Case pauseatla): from the first tick of free play both players' lives are
    // kept full, so a test of a pause is not ended by monsters downing the players first (the host stands alone on the floor for as
    // long as the joiner's boot takes).
    if (lives_full && NetplayTestPressClock() >= 0 && UserStatus != nullptr) {
        const int chara = (int) UserStatus->cur_chara;

        UserStatus->hp[chara] = UserStatus->max_hp[chara];
        UserStatus->next_hp[chara] = UserStatus->hp[chara];
        if (g_partner.loaded_chara >= 0 && g_partner.down <= 0) {
            SetLife(MaxLife());
        }
    }

    if (spec == nullptr || NowColData == nullptr || std::sscanf(spec, "%d:%ld:%d", &player, &when, &damage) != 3) {
        return;
    }
    // coop-menuflake: until the hit both players' lives are full and neither is invincible, so what the test shows is the hit and
    // not how long the floor's monsters worked on them first (the host stands alone on the floor for as long as the joiner's
    // boot takes, thousands of ticks on a loaded PC: Toan was at 35, 18 or 0 life, or still invincible, when it came).
    if (NetplayTestPressClock() >= 0 && NetplayTestPressClock() < when && UserStatus != nullptr) {
        const int chara = (int) UserStatus->cur_chara;

        UserStatus->hp[chara] = UserStatus->max_hp[chara];
        UserStatus->next_hp[chara] = UserStatus->hp[chara];
        BtActStatus.invincible_frames = 0;
        if (g_partner.loaded_chara >= 0 && g_partner.down <= 0) {
            SetLife(MaxLife());
            g_partner.invincible = 0;
        }
    }
    // Twenty ticks on, and again at 100, 200, 300 and 400 (coop-menuflake: the hit can land well after the tick it is set, so
    // the test takes the lowest): the lives it left.
    const long since_hit = NetplayTestPressClock() - when;

    if ((since_hit == 20 || since_hit == 100 || since_hit == 200 || since_hit == 300 || since_hit == 400) && UserStatus != nullptr) {
        int p2 = g_partner.loaded_chara;
        std::printf("coop-pause: after the test hit P1 life %d, P2 life %d down %d\n", (int) UserStatus->hp[UserStatus->cur_chara],
                    p2 >= 0 ? Life() : -1, g_partner.down);
    }
    // coop-menuflake: a scripted hit is sometimes not taken (the target's hit test did not meet it that tick), so until the target's
    // life has dropped it is set again every 10 ticks for 120 ticks (the first one logs, as before).
    static int first_life = -1;
    const int  life_now = player == 1 ? (UserStatus != nullptr ? (int) UserStatus->hp[UserStatus->cur_chara] : -1) : (g_partner.loaded_chara >= 0 ? Life() : -1);
    const long retry = NetplayTestPressClock() - when;

    if (retry > 0 && retry <= 120 && retry % 10 == 0 && first_life >= 0 && life_now >= first_life) {
        float again[4] = {0.0f, 0.0f, 0.0f, 1.0f};

        for (int i = 0; i < 3; i++) {
            again[i] = player == 1 ? CharaMain.pos[i] : g_partner.pos[i];
        }
        NowColData->Set(again, damage, 2, 8.0f, 0.0f, 1, 2, 0, 0);
    }
    if (NetplayTestPressClock() != when) {
        return;
    }
    first_life = life_now;

    float pos[4] = {0.0f, 0.0f, 0.0f, 1.0f};

    for (int i = 0; i < 3; i++) {
        pos[i] = player == 1 ? CharaMain.pos[i] : g_partner.pos[i];
    }
    int slot = NowColData->Set(pos, damage, 2, 8.0f, 0.0f, 1, 2, 0, 0);
    std::printf("coop-pause: test hit of %d on P%d in slot %d (P1 life %d, P2 life %d, P2 down %d, P1 paused %d, P2 paused %d)\n", damage, player, slot,
                UserStatus != nullptr ? (int) UserStatus->hp[UserStatus->cur_chara] : -1,
                g_partner.loaded_chara >= 0 ? Life() : -1, g_partner.down, (int) g_pause.paused[0],
                (int) g_pause.paused[1]);
}

// Test hook (DC_COOP_PAUSE_ATLA=1, t-triangle-menu -Case pauseatla): ten ticks into free play the floor's first Atla is
// set 60 in front of Toan on both copies (the same tick, so the copies agree), and every 30 ticks its bob is logged.
void TestAtla() {
    static const bool on = TestEnvInt("DC_COOP_PAUSE_ATLA", 0) != 0;

    if (!on || NowDngMap == nullptr) {
        return;
    }
    long now = NetplayTestPressClock();

    if (now == 10) {
        if (NowDngMap->atra_num <= 0) {
            std::printf("coop-pause: test: no atla on this floor\n");
            return;
        }
        NowDngMap->atra[0].pos[0] = CharaMain.pos[0] + 60.0f;
        NowDngMap->atra[0].pos[1] = CharaMain.pos[1];
        NowDngMap->atra[0].pos[2] = CharaMain.pos[2];
        NowDngMap->atra[0].used = 1;
        std::printf("coop-pause: test: atla 0 set at (%.0f %.0f)\n", NowDngMap->atra[0].pos[0], NowDngMap->atra[0].pos[2]);
    }
    if (now > 10 && now % 30 == 0 && NowDngMap->atra_num > 0) {
        std::printf("coop-pause: atla 0 phase %.4f, drawn %d, held %d\n", NowDngMap->atra[0].phase, g_atla_drawn, (int) g_pause.held);
    }
}
// Test hook (DC_COOP_TEST_WAKE=P:D:T, t-triangle-menu -Case floorse, coop-fixtures 2026-11-01): at press clock T the floor's
// first sleeping monster is put D from player P and woken, on both copies (the same tick), so her Circle has a monster to
// lock on and its lock sound is heard (or not) on each copy. (Her field Square is quiet with nothing to use since
// coop-playfix, so floorse's refuse sound no longer exists; the lock sound is the floor's menu sound left to test.)
void TestWake() {
    static const char *spec = TestEnv("DC_COOP_TEST_WAKE");
    int                player = 0;
    long               when = 0;
    int                distance = 0;

    if (spec == nullptr || NowMonstorUnit == nullptr || std::sscanf(spec, "%d:%d:%ld", &player, &distance, &when) != 3 ||
        NetplayTestPressClock() != when) {
        return;
    }
    const float *beside = player == 1 ? CharaMain.pos : g_partner.pos;

    for (int i = 0; i < 16; i++) {
        if (NowMonstorUnit->monster[i].state != 1 || NowMonstorUnit->monster[i].hp <= 0) {
            continue;
        }
        float at[4] = {beside[0] + (float) distance, beside[1], beside[2], 1.0f};

        NowMonstorUnit->chara[i][0].SetPosition(at);
        NowMonstorUnit->monster[i].state = 2;
        std::printf("coop-pause: test: monster %d woken %d from P%d at (%.0f %.0f)\n", i, distance, player, at[0], at[2]);
        return;
    }
    std::printf("coop-pause: test: no sleeping monster to wake\n");
}
// Once per pause: which picture it drew ("coop-pause: drew the retail|caption pause").
void LogPauseDraw(const char *kind) {
    static int last = -1;
    int        now = (int) (g_pause.paused[0] || g_pause.paused[1]);

    if (last != now) {
        last = now;
        std::printf("coop-pause: drew the %s pause\n", kind);
    }
}

} // namespace

bool PauseOn() {
    return MenusOn() && !MenuPanelOn();
}

void PauseTick(const InputPadState &one_in, const InputPadState &two_in) {
    // Linked: each player's own agreed pad (the menus' routed pads hand port 1 to player 2 while her menu is open).
    InputPadState one = one_in, two = two_in;

    NetplayPlayerPad(1, one);
    NetplayPlayerPad(2, two);

    const bool start[2] = {(one.buttons & kInputStart) != 0, (two.buttons & kInputStart) != 0};
    bool       pressed[2];

    for (int i = 0; i < 2; i++) {
        pressed[i] = start[i] && !g_pause.prev_start[i];
        g_pause.prev_start[i] = start[i];
    }

    if (!OnFloor()) {
        // Off the floor (or the game's own tasks: a chest, an event, retail's menu) nobody is paused here.
        if (g_pause.paused[0] || g_pause.paused[1]) {
            std::printf("coop-pause: pauses end (mode %d task %d)\n", mode, (int) gameTask);
        }
        g_pause.paused[0] = g_pause.paused[1] = false;
        Hold(false);
        PresentTestTick(false); // coop-opt7: test hook (a copy in a presentation task, the old path)
        return;
    }

    for (int i = 0; i < 2; i++) {
        int player = i + 1;

        if (g_pause.paused[i] && (Down(player) || InMenu(player) || (player == 2 && !PartnerInZone()))) {
            g_pause.paused[i] = false;
            std::printf("coop-pause: P%d leaves the pause (%s)\n", player, Down(player) ? "down" : InMenu(player) ? "in the menu" : "away");
            continue;
        }
        // Start in one's own menu is the menu's; a downed player has no pause.
        if (pressed[i] && !InMenu(player) && !Down(player) && !PlayerPresenting(player) && (player == 1 || PartnerInZone())) {
            g_pause.paused[i] = !g_pause.paused[i];
            SoundOwner sound(player); // coop-menuse
            SndSePlay(MENU_SOUND_CONFIRM, -1, 0);
            std::printf("coop-pause: P%d %s\n", player, g_pause.paused[i] ? "paused" : "resumes");
            if (g_pause.paused[i]) {
                CloseWheelForPause(player); // coop-pausewheel: one's own ring shuts under one's own pause
            }
        }
    }

    TestHit();
    TestAtla();
    TestWake();
    Hold(PauseZoneHeld() && !FullMenuLive());
    PresentTestTick(g_pause.held); // coop-opt7: test hook
}

bool PausePlayer(int player) {
    return player >= 1 && player <= 2 && g_pause.paused[player - 1];
}

bool PauseZoneHeld() {
    if (!PauseOn() || Mutated("pausezone")) {
        return false;
    }
    // DC_COOP_MUTATE=pauseone (t-pause's twin): one player's pause holds the world, as retail's.
    if (Mutated("pauseone")) {
        return g_pause.paused[0] || g_pause.paused[1];
    }

    // coop-gatepopup (Jep, 2026-10-28): a player in a gate's question, asking or asked, counts as in a pause.
    // coop-opt7 (Jep 11-09): a player in a presentation of their own (a chest's) is in a pause state too: every player of the zone
    // paused in any mix holds the world (his chest and her menu hold it; his chest while she walks does not). DC_COOP_MUTATE=
    // present_runs (twin): a presentation never counts.
    const bool presents_count = !Mutated("present_runs");
    bool       one = g_pause.paused[0] || InMenu(1) || CoopWorldsQuestionHolds(0) || (presents_count && PlayerPresenting(1));
    // coop-pt1106-lockup (Jep, 2026-11-06: a joiner is never made to wait; x-fixture-join-hostmenu-hit): g_partner.visible comes from the world's own
    // step, so with a menu carried across the join the hold kept it false and the hold stood: the joiner landed in a world held till he closed
    // it, the opener out of all harm. Linked in the lockstep the partner is here, on one tick on both copies. DC_COOP_MUTATE=join_menu_holds_world (twin): as before.
    const bool joined = NetplayLinkUp() && !CoopWorldsApart() && NetplayTestLockstepFrom() >= 0 && NetplayLinkTick() >= NetplayTestLockstepFrom() &&
                        !Mutated("join_menu_holds_world");
    bool two = !(PartnerInZone() || joined) || g_pause.paused[1] || InMenu(2) || CoopWorldsQuestionHolds(1) || (presents_count && PlayerPresenting(2));

    // coop-localfix (Jep, 2026-10-26: "both players being in the triangle menu didnt pause the game in the dungeon"):
    // every player of the zone in a menu holds it too, as one player's menu does in the game alone; a pause was needed
    // before. DC_COOP_MUTATE=menus_run (twin): menus alone never hold it, as before.
    if (Mutated("menus_run")) {
        return one && two && (g_pause.paused[0] || g_pause.paused[1]);
    }
    return one && two;
}

void PauseFilterPort(PAD_STATUS *status) {
    if (!OnFloor()) {
        return;
    }
    if (g_pause.paused[0] && !FullMenuHoldsPartner()) {
        status->button = 0;
        status->left_x = status->left_y = status->right_x = status->right_y = 0x80;
        return;
    }
    if (!FullMenuLive()) {
        status->button &= ~kInputStart;
    }
}

void PauseDraw() {
    if (!PauseOn() || mode != kDungeonMode) {
        return;
    }

    int local = NetplayLocalPlayer();

    for (int i = 0; i < 2; i++) {
        if (!g_pause.paused[i] || (local != 0 && local != i + 1)) {
            continue;
        }

        dc::text::caption::LineStyle style;

        style.status_font = true;
        style.edge_width = 1.5f;
        // The game's own pause picture where retail draws it (gameloop's exitMenuFlag), no dimming (Jep,
        // 2026-10-21). DC_COOP_MUTATE=pausecaption (twin of v-pause-retail) draws the old dim and caption.
        if (Mutated("pausecaption") && local != 0) {
            static const spRGBA kDim = {0x00, 0x00, 0x00, 0x50};

            set2DSpriteC4(Vif1Packet, CRect_i_(0, 0, 640, 448), (spRGBA *) &kDim, (spRGBA *) &kDim, (spRGBA *) &kDim, (spRGBA *) &kDim);
            dc::text::caption::DrawLine(PauseZoneHeld() ? "PAUSE" : "PAUSE  (the world goes on)", 320.0f, 200.0f, 14.0f, 1.0f, 500.0f, style, 1.0f, 1.0f,
                              nullptr);
            LogPauseDraw("caption");
        } else if (local != 0) {
            TexManager.ReloadTexture(Vif1Packet, 7);
            set2DSprite(Vif1Packet, TexManager.GetTexture((char *) "pause", -1), CRect_i_(0x100, 0xCC, 0x80, 0x28), 0, 0);
            LogPauseDraw("retail");
        } else {
            int         chara = i == 0 ? (UserStatus != nullptr ? UserStatus->cur_chara : 0) : g_partner.loaded_chara;
            std::string line = std::string(chara >= 0 && chara < 6 ? kNames[chara] : "Player") + " is paused";

            dc::text::caption::DrawLine(line, i == 0 ? 160.0f : 480.0f, 60.0f, 12.0f, 1.0f, 300.0f, style, 1.0f, 1.0f, nullptr);
        }
    }
}

// The Atla's bob steps in its draw (DrawAtraBoll: phase += 0.1 each frame it is drawn), not in the floor's tick, so the
// world's hold left it bobbing (Jep's playtest, 2026-10-25: "Atla still moves in the world while the game is fully
// paused"). While this module holds the world the bobs are put back after the draw. DC_COOP_MUTATE=pause_atla_bobs:
// left to bob, as before (twin).
void PauseAtlaDrawBegin() {
    g_atla_saved = g_pause.held && NowDngMap != nullptr && !Mutated("pause_atla_bobs");

    for (int i = 0; g_atla_saved && i < 8; i++) {
        g_atla_phase[i] = NowDngMap->atra[i].phase;
    }
}

// (coop-opt7: AtlasShownThroughHis, pt1109c's 1p bandaid, is gone: his Atla's status, atraGetStatus, is set on his copy alone now.)

void PauseAtlaDrawEnd(const float *pos) {
    if (NowDngMap != nullptr && NowDngMap->atra_num > 0 && NowDngMap->atra[0].used != 0 && NowDngMap->atra_model != nullptr &&
        DistVector(NowDngMap->atra[0].pos, (float *) pos) <= 160.0f * NowDngMap->draw_dist_scale) {
        g_atla_drawn++;
    }
    for (int i = 0; g_atla_saved && NowDngMap != nullptr && i < 8; i++) {
        NowDngMap->atra[i].phase = g_atla_phase[i];
    }
    g_atla_saved = false;
}

// coop-chestworld (Jep, 2026-10-30: the world keeps running for player 2 while player 1 opens a chest or takes an Atla):
// retail's presentations (BtGetTreasureboxBig_Init and _Loop, BtGetTreasureboxSmall_Loop, BtAtraGetShort_Loop and
// BtGetGateKey_Loop) set CMonUnitHold, CMonUnitHyde, CEffectHold and CEffectHyde every tick they run (BtGetAttach_Loop the
// two holds; nothing enters its task), so every monster and effect stood still for both players until he closed it. The
// same for his Revival Powder (the two holds from GAME_TASK_DEAD through REVIVE_WAIT and REVIVE_DONE, until his Cross) and a
// drink from his quick slot (the two holds while BtActStatus.action_on is 3, in play, until BattleActionDrink's motion ends;
// Jep, 2026-10-31: the drinker is not sheltered, so he can be hit as in play). His Escape needs nothing here: the pair splits
// as it starts (coop-worlds), so her copy never runs BtEscape_Init's holds.
// The other ways one player stopped the world for both (audit, 2026-11-01): driveStepHold (motionDrive's other hold) and every
// BtGameModeFlag but PLAY (MoveChara, with motionDrive, runs only in PLAY). Each is someone else's already: the pause and the
// menus (this file, coop_fullmenu), his Select ring (coop_wheel's own rings), his knocked-out member change
// (CoopKnockoutEndsForPartner), a door's item window (coop-keypick, his picks too), the gate's escape window
// (EscapeWindowAnswer), the floor menu (coop-gatepopup2), floor scripts (BtEventMode: the scene joins); ATRA_REFUSE and
// ATTACH_LOOP no task enters, and the debug menu is not a player's. With player 2 on the floor (the partner in the zone, the same on both copies) they are
// let go after the task's step, before motionDrive steps the monsters: the monsters and effects run and are drawn on both
// copies alike, as in play (a monster's motion steps in its draw, so hiding them on one copy alone would part the copies).
// The opener stays out of harm: retail's damage check (BtCheckDamageProc) runs only in play, and hers is PartnerPresenting.
// DC_COOP_MUTATE=chestworld (twin): retail's holds, as before.
bool PresentationTask() {
    return gameTask == GAME_TASK_BOX_BIG_INIT || gameTask == GAME_TASK_BOX_BIG_LOOP || gameTask == GAME_TASK_BOX_SMALL_INIT ||
           gameTask == GAME_TASK_BOX_SMALL_LOOP || gameTask == GAME_TASK_ATRA_GET_INIT || gameTask == GAME_TASK_ATRA_GET_LOOP ||
           gameTask == GAME_TASK_GATE_KEY_LOOP || gameTask == GAME_TASK_ATTACH_LOOP;
}

void PresentationWorldRuns() {
    static int said = -1;
    const bool presentation = PresentationTask();
    const bool revive = gameTask == GAME_TASK_REVIVE_WAIT || gameTask == GAME_TASK_REVIVE_DONE;
    const bool drink = gameTask == GAME_TASK_PLAY && BtActStatus.action_on == 3;

    if (mode != kDungeonMode || BtEventMode != 0 || !(presentation || revive || drink) || !PartnerInZone() || g_pause.held ||
        Mutated("chestworld")) {
        said = -1;
        return;
    }

    if (said != (int) gameTask && (CMonUnitHold != 0 || CEffectHold != 0)) {
        said = (int) gameTask;
        std::fprintf(stderr, "coop-chestworld: the world runs through P1's %s (task 0x%x)\n",
                     drink ? "drink" : revive ? "revival" : "presentation", (int) gameTask);
    }

    CMonUnitHold = 0;
    CMonUnitHyde = 0;
    CEffectHold = 0;
    CEffectHyde = 0;
}

std::uint32_t PauseHash() {
    return (g_pause.paused[0] ? 1u : 0u) | (g_pause.paused[1] ? 2u : 0u) | (g_pause.held ? 4u : 0u);
}

void PauseResetForSession() {
    g_pause.paused[0] = g_pause.paused[1] = false;
    Hold(false);
}

} // namespace coop
