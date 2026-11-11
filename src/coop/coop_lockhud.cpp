#include "coop/game_ns.hpp"
#include "coop/coop_lockhud.hpp"

#include <libvu0.h>

#include <cstdio>

#include "clsmes.hpp"
#include "dun/gameloop.hpp"
#include "mathutil.hpp"
#include "menu_misc.hpp"
#include "mglib.hpp"
#include "monstorunit.hpp"
#include "rect.hpp"
#include "snd.hpp"
#include "texture.hpp"

#include "coop/coop.hpp"
#include "coop/coop_combat.hpp"
#include "coop/coop_core.hpp"
#include "coop/netplay.hpp"

namespace dc::game {
// (ENEMY_LIFE_GAGE and EnemyLifeGage: game/dun/gameloop.hpp names them at 9418a550.)
extern s32             lockOnTargetNo;
extern spRGBA          ELife1; // the life bar's two ends (retail's red and orange)
extern spRGBA          ELife2;
extern ClsMes         *CharaNameMes; // repin: menu_misc.cpp, out of its anonymous namespace (recipe 0032)
} // namespace dc::game

namespace coop {

namespace {

// The dim the other player's corners draw at, as the partner's always were.
constexpr int kOtherAlpha = 0x50;

// coop-lockcolour: player 2's bar runs from a deep teal to the map arrow's (CoopMiniMapOtherColour); her corners' texture
// is tinted the arrow's.
constexpr spRGBA kTealLow = {0x00, 0x90, 0x78, 0x80};
constexpr spRGBA kTealHigh = {0x10, 0xFF, 0xC0, 0x80};

struct State {
    LockHudShown shown;
    LockHudShown last;
    bool         drawing_player = false; // inside setTargetCursor on this frame
    bool         swapped = false;        // Begin took the player's name and bar off this copy
    ClsMes      *name_mes = nullptr;
    s32          bar_draw = 0;
};

State g;

bool Fixed() {
    return Enabled() && NetplayLocalPlayer() != 0 && !Mutated("lockhud");
}

// Whether monster i stands within its lock range of where this copy's own player stands.
bool NearOwnPlayer(int i) {
    if (i < 0 || i >= 16 || NowMonstorUnit == nullptr) {
        return false;
    }

    float position[4];

    NowMonstorUnit->chara[i][0].GetPosition(position);
    return DistVector(position, LocalIsPartner() ? g_partner.pos : CharaMain.pos) < NowMonstorUnit->monster[i].lock_range;
}

void Log() {
    static LockHudShown logged;
    static bool         any = false;
    const LockHudShown &s = g.last;

    if (!CoopDebug() || (any && s.name == logged.name && s.bar == logged.bar && s.player_marker == logged.player_marker &&
                         s.partner_marker == logged.partner_marker && s.preview == logged.preview && s.bar_teal == logged.bar_teal &&
                         s.player_teal == logged.player_teal && s.partner_teal == logged.partner_teal)) {
        return;
    }

    any = true;
    logged = s;
    std::printf("coop-lockhud: player %d hud name %d bar %d, corners p1 %d p2 %d, preview %d (p1 lock %d, p2 lock %d); colours bar %s, "
                "corners p1 %s p2 %s\n",
                NetplayLocalPlayer(), s.name, s.bar, s.player_marker, s.partner_marker, s.preview, lockOnTargetFlag != 0 ? (int) lockOnTargetNo : -1,
                (int) g_combat.lock, s.bar < 0 ? "-" : s.bar_teal ? "teal" : "retail", s.player_marker < 0 ? "-" : s.player_teal ? "teal" : "retail",
                s.partner_marker < 0 ? "-" : s.partner_teal ? "teal" : "retail");
}

// The partner's lock's name and bar where setTargetCursor puts the player's: the name 10 above the lock-on point, the
// bar 42 pixels under it.
void DrawPartnerName() {
    int lock = g_combat.lock;

    if (lock < 0 || NowMonstorUnit == nullptr) {
        return;
    }

    const MONSTOR &monster = NowMonstorUnit->monster[lock];

    if (monster.name_no <= 0 || monster.kind == MONSTER_KIND_NO_LOCK_ON) {
        return;
    }

    float target[4];
    int   screen[4];

    NowMonstorUnit->chara[lock][0].GetPosition(target);
    target[1] += 8.0f;

    if (monster.lockon_frame != nullptr) {
        sceVu0CopyVector(target, (float *) monster.lockon_position);
    }

    target[1] += 10.0f;
    target[3] = 1.0f;

    if (MGRotTransPers(screen, target, 0) == 0) {
        return;
    }

    int x = (screen[0] - 0x6C00) >> 4;
    int y = (screen[1] - GS_Y_OFFSET) >> 4;

    MonsterNameMake(monster.name_no);
    MonsterNamePosSet(x, y);
    SetMonsterNameDrawFlag(1);
    g.shown.name = lock;

    // The bar is the game's own (DrawTargetLife), with the player's put back after; hers in teal (coop-lockcolour).
    ENEMY_LIFE_GAGE player = EnemyLifeGage;
    const spRGBA    low = ELife1;
    const spRGBA    high = ELife2;
    // Jep 11-09: a monster's life is not a player's, so the bar is retail's colour for both players on both copies; only the corners
    // are the player's colour. DC_COOP_MUTATE=barcolour (twin): her lock's bar teal, as coop-lockcolour had it.
    const bool      teal = LockHudP2Teal() && Mutated("barcolour");

    EnemyLifeGage.life_max = monster.max_hp;
    EnemyLifeGage.life = monster.hp;
    EnemyLifeGage.x = x;
    EnemyLifeGage.y = y + 42;
    EnemyLifeGage.on = 1;
    EnemyLifeGage.draw = player.draw;

    if (teal) {
        ELife1 = kTealLow;
        ELife2 = kTealHigh;
    }

    if (EnemyLifeGage.draw != 0) {
        DrawTargetLife();
        g.shown.bar = lock;
        g.shown.bar_teal = teal;
    }

    EnemyLifeGage = player;
    ELife1 = low;
    ELife2 = high;
}

} // namespace

// coop-lockcolour (Jep's 2026-11-04 pack 8 playtest: both players' lock-on bars looked alike): player 2's life bar and
// lock corners are teal on either copy, player 1's keep retail's, as the map arrows (one colour per player:
// P1 pink, P2 teal). Couch co-op too (her corners). Draw only. DC_COOP_MUTATE=lockcolour (twin): hers in retail's, as before.
bool LockHudP2Teal() {
    return Enabled() && !Mutated("lockcolour");
}

void LockHudBegin() {
    g.last = g.shown;
    g.shown = LockHudShown{};
    Log();
    g.drawing_player = true;
    g.swapped = Fixed() && LocalIsPartner();

    // On her copy the player's name and bar are not made: MonsterNameMake and MonsterNamePosSet do nothing without a
    // window, and DrawTargetLife nothing with the gauge's draw off. Both come back in End.
    if (g.swapped) {
        g.name_mes = CharaNameMes;
        g.bar_draw = EnemyLifeGage.draw;
        CharaNameMes = nullptr;
        EnemyLifeGage.draw = 0;
    }
}

bool LockHudPlayerPreview() {
    bool shown = !g.swapped;

    if (shown) {
        g.shown.preview = lockOnTargetNo;
    }

    return shown;
}

void LockHudEnd() {
    g.drawing_player = false;

    if (!g.swapped) {
        if (GetMonsterNameDrawFlag() != 0) {
            g.shown.name = lockOnTargetNo;
        }

        return;
    }

    CharaNameMes = g.name_mes;
    EnemyLifeGage.draw = g.bar_draw;
    SetMonsterNameDrawFlag(0);
    DrawPartnerName();
}

int LockHudPartnerCursor(int monster) {
    int alpha = kOtherAlpha;

    if (Fixed()) {
        if (LocalIsPartner()) {
            alpha = 0x80;
        } else if (!NearOwnPlayer(monster)) {
            return 0;
        }
    }

    g.shown.partner_marker = monster;
    g.shown.partner_teal = LockHudP2Teal();
    return alpha;
}

void DrawLockCorners(float *world, float width, float height, int alpha, bool teal) {
    CTexture *texture = TexManager.GetTexture((char *) "d01e04", -1);
    int       top_left[4];
    int       bottom_right[4];

    world[3] = 1.0f;

    if (texture == nullptr || MGRotTransPers3DSprite(top_left, bottom_right, world, width, height, 0) == 0) {
        return;
    }

    top_left[0] = (top_left[0] - 0x6C08) >> 4;
    top_left[1] = (top_left[1] - (GS_Y_OFFSET + 8)) >> 3;
    bottom_right[0] = (bottom_right[0] - 0x6C08) >> 4;
    bottom_right[1] = (bottom_right[1] - (GS_Y_OFFSET + 8)) >> 3;

    // Untinted is 0x80 grey, what the alpha-only set2DSprite draws with.
    const unsigned char red = teal ? kTealHigh.r : 0x80;
    const unsigned char green = teal ? kTealHigh.g : 0x80;
    const unsigned char blue = teal ? kTealHigh.b : 0x80;

    setbilinear(0);
    set2DSprite(Vif1Packet, texture, CRect_i_(top_left[0], top_left[1], 0x10, 0x10), CRect_i_(0, 0x20, 0x10, 0x10), red, green, blue, alpha);
    set2DSprite(Vif1Packet, texture, CRect_i_(bottom_right[0], top_left[1], 0x10, 0x10), CRect_i_(0, 0, 0x10, 0x10), red, green, blue, alpha);
    set2DSprite(Vif1Packet, texture, CRect_i_(top_left[0], bottom_right[1], 0x10, 0x10), CRect_i_(0, 0x30, 0x10, 0x10), red, green, blue, alpha);
    set2DSprite(Vif1Packet, texture, CRect_i_(bottom_right[0], bottom_right[1], 0x10, 0x10), CRect_i_(0, 0x10, 0x10, 0x10), red, green, blue, alpha);
    setbilinear(1);
}

const LockHudShown &LockHudLastShown() {
    return g.last;
}

} // namespace coop

using namespace coop;

// Retail's draws the player's lock corners at the alpha of its first call ever (a function static), then its gauge.
// On the copy whose player is P2 the corners are the other player's: dim, and only while the monster is near her; the
// gauge is not drawn there (LockHudBegin turned it off).
namespace dc::game { // repin: was PORT_OVERRIDE; a recipe removes the game's own definition
void DrawtargetCursor(float *world, float width, float height, float alpha) {
    static int rate = (int) (16.0f * alpha);
    int        shown = rate;

    setbilinear(0);
    world[3] = 1.0f;

    if (g.swapped && !NearOwnPlayer(lockOnTargetNo)) {
        shown = 0;
    } else if (g.swapped) {
        shown = kOtherAlpha;
    }

    if (shown != 0) {
        DrawLockCorners(world, width, height, shown, false); // player 1's: retail's
        g.shown.player_marker = lockOnTargetNo;
        g.shown.player_teal = false;
    }

    setbilinear(1);
    DrawTargetLife();

    if (EnemyLifeGage.on != 0 && EnemyLifeGage.draw != 0) {
        g.shown.bar = lockOnTargetNo;
    }
}
} // namespace dc::game
