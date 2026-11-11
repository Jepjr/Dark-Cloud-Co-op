#include "coop/game_ns.hpp"
#include "coop/coop.hpp"
#include "coop/coop_worlds.hpp" // coop-worlds
#include "coop/coop_perf.hpp"
#include <cstdint>
#include "coop/netplay.hpp"

#include <SDL3/SDL.h>
#include <libvu0.h>

#include <algorithm>
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <map>
#include <new>

#include "btactstatus.hpp"
#include "text/caption_text.hpp"
#include "rect.hpp"
#include "shot_freefuncs.hpp"
#include "snd.hpp"
#include "btitem.hpp"
#include "camerafollow.hpp"
#include "character.hpp"
#include "clothread.hpp"
#include "collision.hpp"
#include "collisiondata.hpp"
#include "dataalloc.hpp"
#include "dataread.hpp"
#include "dranmapfield.hpp"
#include "dun/gameloop.hpp"
#include "dngmessageman.hpp"
#include "dungeoneventdata.hpp"
#include "dungeoneventman.hpp"
#include "dungeonmap.hpp"
#include "sysmes.hpp"
#include "frame.hpp"
#include "frameattr.hpp"
#include "gamepad.hpp"
#include "gameutil.hpp"
#include "hitvalue.hpp"
#include "itemdata.hpp"
#include "menu_draw.hpp"
#include "mglib.hpp"
#include "monstorunit.hpp"
#include "input/mouse_collision.hpp"
#include "shot_utils.hpp"
#include "platform/config.hpp"
#include "platform/input.hpp"
#include "menu_dungeon.hpp"
#include "texture.hpp"
#include "textureanime.hpp"
#include "menu_inventory.hpp"
#include "userstatus.hpp"

#include "battlemenu/internal.hpp"
#include "itemdata.hpp"
#include "menu_draw.hpp"
#include "coop/coop_combat.hpp"
#include "coop/coop_core.hpp"
#include "coop/coop_inventory.hpp" // coop-inventory
#include "coop/coop_party.hpp"
#include "coop/coop_interact.hpp" // coop-floorgate: DrawCallQuestion
#include "coop/coop_wheel.hpp"
#include "text/captionfont.hpp"        // coop-statusicons
#include "render/draw2d.hpp"        // coop-statusicons
#include "localize/localize.hpp"           // coop-statusicons
#include "platform/imagefile.hpp" // coop-statusicons
#include "text/textwipe.hpp"           // coop-statusicons

namespace dc::game {
int keyCtrl(float x, float y, MOTION_INFO *motion);

extern CFrameVu1 *CharaFrame;
extern CTexAnimeData CharaMainTexAnim[64]; // game/dun/gameloop.cpp: the player's texture animations (coop-texdup)
extern int        statusAlarmRate;
extern s32        gameTask;
extern s32        iventMarker;
} // namespace dc::game

using namespace coop;

namespace coop {

// Whether this copy's own player is P2 (a linked copy B) with P2 on the floor: its HUD then shows P2's
// status, weapon panel and picture where the game shows the controlled character's. Draw only.
bool LocalIsPartner() {
    return Enabled() && NetplayLocalPlayer() == 2 && g_partner.visible && g_partner.loaded_chara >= 0 && !Mutated("weaponpanel");
}

} // namespace coop

namespace {

std::string ItemName(int item) {
    return item > 0 ? dc::battlemenu::detail::DecodeItemNamePort(item, "?") : std::string("-");
}

// The weapon P2's HUD shows for a member with none equipped (a stand-in that has not joined, as
// Xiao on a fresh save): its character's starting weapon, as joining the party gives it, the one it
// swings (BaseAttack) and holds (LoadWeapon). Filled in for the draw only, never in the save.
// DC_COOP_MUTATE=standin_weapon: an empty one, as before (Toan's dagger stayed in the picture).
WEAPON_HAVE StandInWeapon(int chara) {
    WEAPON_HAVE weapon{};

    if (!Mutated("standin_weapon") && StarterWeapon(chara) > 0) {
        WepDataListToHaveCopy(StarterWeapon(chara), &weapon);
    }

    return weapon;
}

bool g_bars_only = false;
int  g_other_bars = 0; // frames the other player's full bar was drawn (v-p2-bar)

// coop-statusicons (Jep's 2026-10-27 ask): the ailments as Dark Cloud 2's status icons in place of their words, from
// lang/coop/status/<name>.png beside the executable (the mod's own files, 22 by 22 RGBA), put in the caption font's atlas
// and put there again after the atlas starts over. The ailments with no icon (knocked out) draw nothing, as before.
struct StatusIcon {
    int         flag;
    const char *file;
};

constexpr StatusIcon kStatusIcons[] = {{AILMENT_FREEZE, "freeze"}, {AILMENT_STAMINA, "stamina"}, {AILMENT_POISON, "poison"},
                                       {AILMENT_CURSE, "curse"},   {AILMENT_GOO, "gooey"}};
constexpr int        kStatusIconCount = sizeof kStatusIcons / sizeof kStatusIcons[0];
constexpr int        kStatusIconSize = 18; // drawn size, in the HUD's 640 by 480 places

// The icon's place in the atlas; false while it cannot be read (said once in the log).
bool StatusIconGlyph(int index, dc::text::capfont::Glyph &out) {
    struct Picture {
        int                  width = 0, height = 0;
        std::vector<uint8_t> rgba;
        bool                 tried = false;
    };
    static Picture        pictures[kStatusIconCount];
    static dc::text::capfont::Glyph glyphs[kStatusIconCount];
    static unsigned       generation[kStatusIconCount] = {};
    static bool           placed[kStatusIconCount] = {};
    Picture              &picture = pictures[index];

    if (!picture.tried) {
        const std::string           name = std::string("coop/status/") + kStatusIcons[index].file + ".png";
        const std::filesystem::path path = dc::localize::LocalizeFindFile(name);

        picture.tried = true;
        if (path.empty() || !imagefile::LoadPng(path, picture.width, picture.height, picture.rgba)) {
            picture.rgba.clear();
            std::fprintf(stderr, "coop-hud: no status icon lang/%s\n", name.c_str());
        }
    }
    if (picture.rgba.empty()) {
        return false;
    }
    if (!placed[index] || generation[index] != dc::text::capfont::Generation()) {
        placed[index] = dc::text::capfont::AddImage(picture.rgba, picture.width, picture.height, glyphs[index]);
        generation[index] = dc::text::capfont::Generation();

        if (!placed[index]) {
            return false;
        }
    }

    out = glyphs[index];
    return true;
}

// chara's ailment icons in a row from x, centred on y; how many were drawn. DC_COOP_MUTATE=statusicons_off draws none
// (the twin of v-status-icons).
int DrawStatusIcons(int chara, float x, float y) {
    if (chara < 0 || chara >= 6 || Mutated("statusicons_off")) {
        return 0;
    }

    std::vector<gfx::Vertex2D> quads;

    for (int i = 0; i < kStatusIconCount; i++) {
        dc::text::capfont::Glyph glyph;

        if ((UserStatus->ailments[chara] & kStatusIcons[i].flag) == 0) {
            continue;
        }
        if (!StatusIconGlyph(i, glyph)) {
            continue;
        }

        // (The atlas may start over while the row is put together: then every one is put in again next frame.)
        const float left = x, top = y - kStatusIconSize / 2.0f, s = (float) kStatusIconSize;

        quads.push_back(dc::render::draw2d::Vertex(left, top, 0.0f, glyph.u0, glyph.v0, 0x80, 0x80, 0x80, 0x80));
        quads.push_back(dc::render::draw2d::Vertex(left + s, top, 0.0f, glyph.u1, glyph.v0, 0x80, 0x80, 0x80, 0x80));
        quads.push_back(dc::render::draw2d::Vertex(left + s, top + s, 0.0f, glyph.u1, glyph.v1, 0x80, 0x80, 0x80, 0x80));
        quads.push_back(dc::render::draw2d::Vertex(left, top + s, 0.0f, glyph.u0, glyph.v1, 0x80, 0x80, 0x80, 0x80));
        x += s + 2.0f;
    }

    if (!quads.empty()) {
        dc::text::textwipe::Draw2D(gfx::Primitive::Quads, quads, dc::text::capfont::Binding(), dc::render::draw2d::SpriteState());
        dc::render::draw2d::RestoreTestZbuf();
    }

    return (int) quads.size() / 4;
}

// coop-sendsame: a send's notice under the other player's bar, logged (stderr) the first frame each one draws, so
// v-no-r3-menu -Case p1 sees that pad 1's R3 draws nothing there.
void DrawNotice(float y, const dc::text::caption::LineStyle &style) {
    static std::string logged;

    if (logged != InventoryNotice()) {
        logged = InventoryNotice();
        std::fprintf(stderr, "coop-hud: notice under the other bar: %s\n", logged.c_str());
    }

    dc::text::caption::DrawLine(InventoryNotice(), 0x30 + 110.0f, y, 8.0f, 1.0f, 220.0f, style, 1.0f, 1.0f, nullptr);
}

// The other player's bar: topStatusInfo's life, weapon (with its ABS) and water rows for that player, at y as
// topStatusInfo takes it. chara: the partner's member, its life Life() and a stand-in's starting weapon put in for the
// call only, as CoopTopStatusInfo does; -1: the player as the save holds it. Nothing in the tick sees the swap.
void DrawOtherBar(int chara, int y) {
    CUserStatus *status = UserStatus;
    s8           cur_chara = status->cur_chara;
    int          whose = chara >= 0 ? chara : cur_chara;
    s16          hp = status->hp[whose];
    s16          max_hp = status->max_hp[whose];
    s8           slot = status->equipped_weapon_slot[whose];
    WEAPON_HAVE  slot0 = status->chara_weapons[whose][0];

    if (chara >= 0) {
        status->cur_chara = (s8) chara;
        status->hp[chara] = (s16) Life();
        status->max_hp[chara] = (s16) MaxLife();

        if (EquippedWeapon(chara) == nullptr) {
            status->chara_weapons[chara][0] = StandInWeapon(chara);
            status->equipped_weapon_slot[chara] = 0;
        }
    }

    g_bars_only = true;
    topStatusInfo(y, 0, 0);
    g_bars_only = false;
    g_other_bars++;

    status->cur_chara = cur_chara;
    status->hp[whose] = hp;
    status->max_hp[whose] = max_hp;
    status->equipped_weapon_slot[whose] = slot;
    status->chara_weapons[whose][0] = slot0;

    static int  log_wait = 0;
    static bool load_pending = NetplayStateLoadPending();

    // coop-fx51e (v-p2-bar on a fixture): the line again on the first bar after a state fixture was put back, so a test reads
    // the bar the fixture's game drew, not the one before the load.
    if (load_pending && !NetplayStateLoadPending()) {
        log_wait = 0;
    }
    load_pending = NetplayStateLoadPending();
    if (--log_wait <= 0) {
        log_wait = 300;
        std::printf("coop-hud: p%d drew %s's full bar (member %d), %d frames\n", NetplayLocalPlayer(), chara >= 0 ? "player 2" : "player 1",
                    whose, g_other_bars);
    }
}

// Under the partner's life bar: its charge, its weapon and the item Square uses, and, while it is
// open, its panel of weapons or items.
void CoopDrawCombatHud(int y) {
    int                chara = g_partner.loaded_chara;
    const WEAPON_HAVE *weapon = EquippedWeapon(chara);
    dc::text::caption::LineStyle style;

    style.status_font = true;
    style.edge_width = 1.5f;

    if (g_combat.charging) {
        static const spRGBA kLevel[3] = {{0x60, 0x60, 0x60, 0x80}, {0x80, 0x70, 0x20, 0x80}, {0x80, 0x30, 0x20, 0x80}};
        static const spRGBA kTrough = {0x10, 0x10, 0x10, 0x60};
        int                 fill = std::min(60, g_combat.charge_ticks * 60 / 90);

        set2DSpriteC4(Vif1Packet, CRect_i_(0x33, y + 17, 60, 4), (spRGBA *) &kTrough, (spRGBA *) &kTrough, (spRGBA *) &kTrough,
                      (spRGBA *) &kTrough);
        set2DSpriteC4(Vif1Packet, CRect_i_(0x33, y + 17, fill, 4), (spRGBA *) &kLevel[g_combat.charge_level],
                      (spRGBA *) &kLevel[g_combat.charge_level], (spRGBA *) &kLevel[g_combat.charge_level],
                      (spRGBA *) &kLevel[g_combat.charge_level]);
    }

    // Down, the bar under the life shows how far the player's standing by has got it up.
    if (g_partner.down > 0) {
        static const spRGBA kRevive = {0x30, 0x80, 0x40, 0x80};
        static const spRGBA kTrough = {0x10, 0x10, 0x10, 0x60};
        int                 fill = std::min(60, g_partner.revive * 60 / std::max(1, kReviveTicks));

        set2DSpriteC4(Vif1Packet, CRect_i_(0x33, y + 17, 60, 4), (spRGBA *) &kTrough, (spRGBA *) &kTrough, (spRGBA *) &kTrough,
                      (spRGBA *) &kTrough);
        set2DSpriteC4(Vif1Packet, CRect_i_(0x33, y + 17, fill, 4), (spRGBA *) &kRevive, (spRGBA *) &kRevive, (spRGBA *) &kRevive,
                      (spRGBA *) &kRevive);
        dc::text::caption::DrawLine(g_partner.revive > 0 ? "reviving" : "down: P1 stand by to revive", 0x30 + 110.0f, (float) y + 34.0f, 8.0f, 1.0f,
                          220.0f, style, 1.0f, 1.0f, nullptr);
        return;
    }

    // Under the full second bar (Jep, 2026-10-21) the weapon is its weapon row: no line of text, only a send's notice
    // and the panel. DC_COOP_MUTATE=p2bar_old: the old line under a life bar alone.
    if (!Mutated("p2bar_old")) {
        if (*InventoryNotice() != '\0' && !g_combat.panel) {
            DrawNotice((float) y + 34.0f, style);
        }
    }

    std::string line = ItemName(weapon != nullptr ? weapon->item_no : StandInWeapon(chara).item_no);

    if (!Mutated("p2bar_old")) {
        line.clear();
    } else if (OwnBags()) {
        line += (line.empty() ? "" : "   ") + SlotsLine(); // coop-inventory: P2's quick-use slots
    } else if (g_combat.item > 0) {
        line += (line.empty() ? "" : "   ") + ItemName(g_combat.item) + " x" + std::to_string(ItemCount(g_combat.item));
    }

    if (Mutated("p2bar_old")) {
        if (g_combat.lock >= 0) {
            line += line.empty() ? "[lock]" : "   [lock]";
        }

        line += line.empty() ? "L3 swap" : "   L3 swap";
        dc::text::caption::DrawLine(line, 0x30 + 110.0f, (float) y + 34.0f, 8.0f, 1.0f, 220.0f, style, 1.0f, 1.0f, nullptr);
    }

    // coop-inventory: the last send, for a few seconds (under the panel's place, so not while it is open).
    if (Mutated("p2bar_old") && *InventoryNotice() != '\0' && !g_combat.panel) {
        DrawNotice((float) y + 44.0f, style);
    }

    if (!g_combat.panel) {
        return;
    }

    int rows[32];
    int count = g_combat.page == 0 ? WeaponSlots(rows, 11) : g_combat.page == 1 ? UsableItems(rows, 32) : BagItems(rows, 32); // coop-inventory
    int first = std::max(0, std::min(g_combat.row - 3, count - 7));
    int top = y + 48;
    int shown = std::min(count - first, 7);

    static const spRGBA kBack = {0x00, 0x00, 0x10, 0x58};

    set2DSpriteC4(Vif1Packet, CRect_i_(0x2C, top - 2, 200, 14 + 11 * std::max(1, shown)), (spRGBA *) &kBack, (spRGBA *) &kBack,
                  (spRGBA *) &kBack, (spRGBA *) &kBack);
    // coop-inventory: the Bag page shows P2's gilda; Square on Items and Cross on Bag send to P1.
    std::string title = g_combat.page == 0 ? "2P  < Weapons >"
                        : g_combat.page == 1 ? (OwnBags() ? "2P  < Items >  Square: send" : "2P  < Items >")
                                             : "2P  < Bag >  " + std::to_string(PartnerGilda()) + " G  Cross: send  Square: slot";
    dc::text::caption::DrawLine(title, 0x30 + 100.0f, (float) top, 8.0f, 1.0f, 190.0f, style, 1.0f, 1.0f, nullptr);

    if (count == 0) {
        dc::text::caption::DrawLine("(none)", 0x30 + 100.0f, (float) top + 12.0f, 8.0f, 1.0f, 190.0f, style, 0.7f, 1.0f, nullptr);
        return;
    }

    for (int i = 0; i < shown; i++) {
        int         at = first + i;
        bool        current = at == g_combat.row;
        std::string text;

        if (g_combat.page == 0) {
            const WEAPON_HAVE &have = UserStatus->chara_weapons[chara][rows[at]];

            text = ItemName(have.item_no) + "  " + std::to_string(have.attack);

            if (rows[at] == UserStatus->equipped_weapon_slot[chara]) {
                text += "  (E)";
            }
        } else {
            text = ItemName(rows[at]) + "  x" + std::to_string(ItemCount(rows[at]));
        }

        dc::text::caption::LineStyle row_style = style;

        if (current) {
            row_style.fill[2] = 120;
            text = "> " + text;
        }

        dc::text::caption::DrawLine(text, 0x30 + 100.0f, (float) top + 12.0f + 11.0f * i, 8.0f, 1.0f, 190.0f, row_style, current ? 1.0f : 0.75f,
                          1.0f, nullptr);
    }
}

} // namespace

void CoopDrawHud(int y) {
    // DC_COOP_TEXDUP=1 (debug probe): at frame 400 of the floor, texture names held by two table slots.
    static int texdup_frames = 0;
    if (TestEnvOn("DC_COOP_TEXDUP") && ++texdup_frames % 600 == 150) {
        for (int i = 0; i < 196; i++) {
            for (int j = i + 1; j < 196; j++) {
                const CTexture &a = TexManager.textures[i], &b = TexManager.textures[j];
                if (a.name[0] != 0 && std::strcmp(a.name, b.name) == 0) {
                    std::fprintf(stderr, "coop-texdup: %s slot %d block %d, slot %d block %d, keys %u %u\n", a.name, i, a.block, j, b.block,
                                 (unsigned) (a.tex0 & 0x3FFF), (unsigned) (b.tex0 & 0x3FFF));
                }
            }
        }
        auto owner = [](u_long tex0) {
            for (int k = 0; k < 196; k++) {
                if (TexManager.textures[k].name[0] != 0 && (TexManager.textures[k].tex0 & 0x3FFF) == (tex0 & 0x3FFF)) {
                    return TexManager.textures[k].block * 1000 + k;
                }
            }
            return -1;
        };
        for (int g = 0; g < 24; g++) {
            for (CTexAnimeData *r = BtTexAnime.first[g]; r != nullptr && r <= BtTexAnime.last[g]; r++) {
                std::fprintf(stderr, "coop-texdup: anime %d keys %u->%u owners %d %d blocks %d %d\n", g, (unsigned) (r->first_texture.tex0 & 0x3FFF),
                             (unsigned) (r->second_texture.tex0 & 0x3FFF), owner(r->first_texture.tex0), owner(r->second_texture.tex0),
                             r->first_texture.block, r->second_texture.block);
            }
        }
        for (int k = 0; k < 64; k++) {
            const CTexAnimeData &r = CharaMainTexAnim[k];
            if (r.first_texture.tex0 != 0 || r.second_texture.tex0 != 0) {
                std::fprintf(stderr, "coop-texdup: player anime %d blocks %d %d\n", k, r.first_texture.block, r.second_texture.block);
            }
        }
        for (int k = 0; g_partner.tex_anime != nullptr && k < 64; k++) {
            const CTexAnimeData &r = g_partner.tex_anime[k];
            if (r.first_texture.tex0 != 0 || r.second_texture.tex0 != 0) {
                std::fprintf(stderr, "coop-texdup: partner anime %d keys %u->%u owners %d %d blocks %d %d\n", k, (unsigned) (r.first_texture.tex0 & 0x3FFF),
                             (unsigned) (r.second_texture.tex0 & 0x3FFF), owner(r.first_texture.tex0), owner(r.second_texture.tex0),
                             r.first_texture.block, r.second_texture.block);
            }
        }
        if (texdup_frames == 150) {
            for (int k = 0; k < 196; k++) {
                const CTexture &t = TexManager.textures[k];
                if (t.name[0] != 0 && (t.block == 3 || t.block == 64)) {
                    std::fprintf(stderr, "coop-texdup: slot %d block %d %s key %u clut %u %dx%d\n", k, t.block, t.name, (unsigned) (t.tex0 & 0x3FFF),
                                 (unsigned) ((t.tex0 >> 37) & 0x3FFF), t.width, t.height);
                }
            }
        }
        std::fprintf(stderr, "coop-texdup: scan done\n");
    }

    CoopPerfScope perf(kCoopPerfHud); // coop: perf
    DrawCallQuestion(); // coop-floorgate: the other player's call, apart (no partner drawn then)
    if (!g_partner.visible) {
        // coop-gatepopup: the notice too, alone on a floor (a call's answer; the other copy's news apart): it was drawn only
        // beside the partner's bar. DC_WORLDS_MUTATE=answer_link_line is the answer's twin.
        NetplayDrawNotice();
        return;
    }

    // The partner's life bar, under the player's water, drawn as topStatusInfo draws the player's: the
    // same frame cells of itempack, the same gauge colours and alarm blink, the heart and the numbers,
    // then the character's name.
    static const char *const kNames[6] = {"Toan", "Xiao", "Goro", "Ruby", "Ungaga", "Osmond"};
    CTexture                *icons = TexManager.GetTexture((char *) "itempack", -1);
    CTexture                *frame = TexManager.GetTexture((char *) "stayframe", -1);
    // On the linked copy whose player is P2, the status above is P2's (CoopTopStatusInfo) and this line
    // is the player's, as player 1's copy has it the other way round.
    bool                     mirror = LocalIsPartner();
    int                      player = UserStatus->cur_chara;
    float                    life = (float) (mirror ? UserStatus->hp[player] : Life());
    float                    life_max = (float) std::max(1, mirror ? (int) UserStatus->max_hp[player] : MaxLife());
    int                      length = std::max(2, (int) (0.74f * life_max));
    int                      cells = std::max(2, (length >> 2) + 1);
    int                      alpha = life <= 0.2f * life_max ? statusAlarmRate : 0x80;

    y = y - 10 + BtActStatus.hud_shake_y + 0x3C;

    // The other player's bar is a second copy of the game's own (Jep, 2026-10-21): life, weapon with its ABS and water,
    // drawn by topStatusInfo itself with that player in the controlled character's place; its name beside it.
    // DC_COOP_MUTATE=p2bar_old: the life bar alone with a line of text under it, as before (twin of v-p2-bar).
    if (!Mutated("p2bar_old")) {
        DrawOtherBar(mirror ? -1 : g_partner.loaded_chara, y + 10 - BtActStatus.hud_shake_y);

        int         whose = mirror ? player : g_partner.loaded_chara;
        // coop-hudicon (Jep's 2026-10-27 playtest): each bar has its member's wheel face to its left, centred on its three
        // rows (life, weapon, water), in place of the "2P Xiao" / "1P Toan" tag; on both copies, each for its own player's
        // bar and the other's. The ailments stay as words beside the other bar. DC_COOP_MUTATE=hudface_off: the tag, as before.
        const bool  faces = !Mutated("hudface_off");
        std::string name = faces ? std::string() : (mirror ? std::string("1P ") : std::string("2P ")) + kNames[whose];

        // (The test's report comes in the twin too, with nothing drawn, so its frame checks look at the same places.)
        {
            constexpr int kFace = 30;
            const int     own = mirror ? g_partner.loaded_chara : player;
            const int     face_x = 0x20 - kFace - 1;
            const int     own_y = y - 0x3C + 0x1B - kFace / 2; // topStatusInfo(rogoY3)'s rows start 0x3C above the other bar's
            const int     other_y = y + 0x1B - kFace / 2;
            const bool    own_drawn = faces && CoopDrawHudFace(own, face_x, own_y, kFace);
            const bool    other_drawn = faces && CoopDrawHudFace(whose, face_x, other_y, kFace);

            if (TestEnv("DC_COOP_HUDFACE") != nullptr) {
                std::fprintf(stderr, "coop-hudface: own %d at (%d %d) %d, other %d at (%d %d) %d, size %d, copy of p%d\n", own, face_x, own_y,
                             own_drawn ? 1 : 0, whose, face_x, other_y, other_drawn ? 1 : 0, kFace, NetplayLocalPlayer() == 2 ? 2 : 1);
            }
        }

        // coop-statusicons: each bar's ailments as icons beside its water row, where the other bar's words were (no
        // words now); own bar too, which the game itself leaves without any.
        {
            constexpr float kIconX = 0x30 + 106.0f;
            const int       own = mirror ? g_partner.loaded_chara : player;
            const float     own_y = (float) (y - 0x3C + 0x2E);
            const float     other_y = (float) (y + 0x2E);
            const int       own_icons = DrawStatusIcons(own, kIconX, own_y);
            const int       other_icons = DrawStatusIcons(whose, kIconX, other_y);
            static int      log_wait = 0;

            if (TestEnv("DC_COOP_STATUSICONS") != nullptr && --log_wait <= 0) {
                log_wait = 30;
                std::fprintf(stderr,
                             "coop-statusicons: own %d ailments %#x icons %d at (%.0f %.0f), other %d ailments %#x icons %d at (%.0f %.0f), size %d, "
                             "copy of p%d\n",
                             own, own >= 0 ? (unsigned) UserStatus->ailments[own] : 0u, own_icons, kIconX, own_y, whose,
                             (unsigned) UserStatus->ailments[whose], other_icons, kIconX, other_y, kStatusIconSize, NetplayLocalPlayer() == 2 ? 2 : 1);
            }
        }

        dc::text::caption::LineStyle tag;

        tag.status_font = true;
        tag.edge_width = 1.5f;

        if (!name.empty()) {
            dc::text::caption::DrawLine(name, 0x30 + 130.0f, (float) y + 0x2A, 9.0f, 1.0f, 140.0f, tag, 1.0f, 1.0f, nullptr);
        }

        bool        desync = false;
        const char *link = NetplayStatus(desync);

        if (*CoopWorldsStatus() != ' ') {
            link = CoopWorldsStatus();
        }
        if (*link != '\0') {
            dc::text::caption::LineStyle link_style = tag;

            if (desync) {
                link_style.fill[1] = link_style.fill[2] = 80;
            }
            dc::text::caption::DrawLine(link, 0x30 + 220.0f, (float) y + 0x34, 9.0f, 1.0f, 220.0f, link_style, 1.0f, 1.0f, nullptr);
        }

        NetplayDrawNotice(); // "<name> has joined"
        CoopDrawWheel();     // coop-party: pad 2's character wheel
        CoopDrawCombatHud(y + 0x27); // its charge, revive and panel, under the water row
        return;
    }

    spRGBA *upper = (spRGBA *) BtGetStatusPal(0, life_max, life);
    spRGBA *lower = (spRGBA *) BtGetStatusPal2(0, life_max, life);

    set2DSprite(Vif1Packet, icons, CRect_i_(0x30, y, 4, 0x10), CRect_i_(0x68, 0x70, 4, 0x10), alpha);

    for (int i = 0; i < cells - 2; i++) {
        set2DSprite(Vif1Packet, icons, CRect_i_(i * 4 + 0x34, y, 4, 0x10), CRect_i_(0x6C, 0x70, 4, 0x10), alpha);
    }

    set2DSprite(Vif1Packet, icons, CRect_i_((cells - 1) * 4 + 0x30, y, 4, 0x10), CRect_i_(0x70, 0x70, 4, 0x10), alpha);

    if (life > 0.0f) {
        int trough = cells * 4 - 5;
        int fill = std::clamp((int) ((float) trough * life / life_max), 0, trough);

        set2DSpriteC4(Vif1Packet, CRect_i_(0x33, y + 4, fill, 7), upper, lower, upper, lower);
    }

    int x = (cells - 1) * 4 + 0x38;

    x += ValuePrint(x, y + 7, (int) life, 0, alpha) * 10;
    set2DSprite(Vif1Packet, frame, CRect_i_(x, y + 7, 0xC, 0xC), CRect_i_(0x78, 0xB0, 0xC, 0xC), alpha);
    x += 10 + ValuePrint(x + 10, y + 7, (int) life_max, 0, alpha) * 10;
    set2DSprite(Vif1Packet, icons, CRect_i_(0x20, y, 0x10, 0x10), CRect_i_(0x40, 0xA0, 0x10, 0x10), alpha);

    dc::text::caption::LineStyle style;

    style.status_font = true;
    style.edge_width = 1.5f;

    bool        desync = false;
    const char *link = NetplayStatus(desync);

    // coop-worlds: apart, the line says where the other player is.
    if (*CoopWorldsStatus() != ' ') {
        link = CoopWorldsStatus();
    }

    if (*link != '\0') {
        dc::text::caption::LineStyle link_style;

        link_style.status_font = true;
        link_style.edge_width = 1.5f;

        if (desync) {
            link_style.fill[1] = link_style.fill[2] = 80;
        }

        dc::text::caption::DrawLine(link, 120.0f, (float) y + 22.0f, 9.0f, 1.0f, 220.0f, link_style, 1.0f, 1.0f, nullptr);
    }
    int         whose = mirror ? player : g_partner.loaded_chara;
    std::string label = (mirror ? std::string("1P ") : std::string("2P ")) + kNames[whose] + "  water " +
                        std::to_string((int) UserStatus->water_now[whose]) + "/" + std::to_string((int) UserStatus->water_max[whose]);

    // Its ailments, by name (the game's icons are the player's).
    static const struct {
        int         flag;
        const char *name;
    } kAilments[] = {{AILMENT_FREEZE, "freeze"}, {AILMENT_STAMINA, "stamina"}, {AILMENT_POISON, "poison"}, {AILMENT_CURSE, "curse"},
                     {AILMENT_GOO, "goo"}};

    for (const auto &ailment : kAilments) {
        if ((UserStatus->ailments[whose] & ailment.flag) != 0) {
            label += std::string("  ") + ailment.name;
        }
    }
    dc::text::caption::DrawLine(label, (float) x + 34.0f, (float) y + 5.0f, 9.0f, 1.0f, 80.0f, style, (float) alpha / 128.0f, 1.0f, nullptr);
    NetplayDrawNotice(); // "<name> has joined"
    CoopDrawWheel(); // coop-party: pad 2's character wheel
    CoopDrawCombatHud(y);
}

namespace coop {

bool StatusBarsOnly() {
    return g_bars_only;
}

WeaponPanelShown g_weapon_panel_shown;

} // namespace coop

namespace dc::game {
extern CTexture *TEX_WepGage;
} // namespace dc::game

namespace {

// The weapon pictures the HUD draws by the gauge sit in "itempack"'s reserved corner: the game copies
// the controlled character's weapon, and its two default weapons, there (DngActiveWeaponTextureCopy)
// from the "wepicon" sheet of the menus' itemlst.img, which shares "itempack"'s palette. On the copy
// whose player is P2 the save's controlled character is the host's, so the game keeps copying Toan's
// sword there (Jep, 2026-10-20); P2's are copied here each frame, after the game's own copy. Only the
// palette indices move, so the sheet must be itemlst.img's: allicon.img's "wepicon" has another
// palette, and pictures copied from it showed the right shapes in the wrong colours (Xiao's slingshot,
// and P2's picture after a swap; Jep's playtest notes 22 and 33, 2026-10-25). The sheet is loaded into
// a block of its own and copied from by its block, never looked up by name (a menu's own "wepicon",
// loaded later, would answer a lookup by name first). Texture state only; tried again on a new floor,
// at most once a second when the file will not load.
constexpr int kIconTextureBlock = 0x43;

std::vector<u_char> g_icon_file;
int                 g_icon_wait = 0;
int                 g_icon_source = 0; // 1 itemlst.img (the HUD's palette), 2 allicon.img (the twin's)

CTexture *IconSheet() {
    // The twin (DC_COOP_MUTATE=icon_sheet): allicon.img's sheet, the one the picture used to come from.
    int want = Mutated("icon_sheet") ? 2 : 1;

    if (CTexture *sheet = TexManager.GetTexture((char *) "wepicon", kIconTextureBlock)) {
        if (g_icon_source == want) {
            return sheet;
        }
    }

    if (g_icon_wait > 0) {
        g_icon_wait--;
        return nullptr;
    }

    g_icon_wait = 60;

    if (g_icon_file.empty()) {
        g_icon_file.resize(512 * 1024 + 64);
    }

    u_char *data = g_icon_file.data() + ((64 - reinterpret_cast<std::uintptr_t>(g_icon_file.data()) % 64) % 64);
    int     size = 0;

    if (want == 1) {
        size = LoadFileMenuData((char *) "itemlst.img", reinterpret_cast<unsigned int *>(data));
    } else if (LoadFile((char *) "commenu/allicon.img", data, &size) == 0) {
        size = 0;
    }

    if (size <= 0 || size > 512 * 1024) {
        std::fprintf(stderr, "coop: cannot load %s for the weapon icons\n", want == 1 ? "itemlst.img" : "commenu/allicon.img");
        return nullptr;
    }

    LOADTEXTURE_INFO2 table[2] = {
        {(char *) data, kIconTextureBlock, 0},
        {nullptr,       0,                 0},
    };

    TexManager.DeleteTextureBlock(kIconTextureBlock);
    TexManager.CleanUpTextureList();
    TexManager.LoadTextureBlockEX(kIconTextureBlock, table);
    g_icon_source = want;

    return TexManager.GetTexture((char *) "wepicon", kIconTextureBlock);
}

// One 32 by 32 picture from the sheet into "itempack", as setItemToReserved copies it but from the
// sheet given, not the first of its name.
void CopyIcon(CTexture *sheet, CTexture *page, int item, int x, int y) {
    COM_ITEM_INFO *info = GetCommonItemInfo(item);

    if (info == nullptr || info->icon_index < 0) {
        return;
    }

    int icon = info->icon_index;

    MoveImageTest(Vif1Packet, (int) (sheet->tex0 & 0x3FFF), (int) ((sheet->tex0 >> 14) & 0x3F), (int) ((sheet->tex0 >> 20) & 0x3F),
                  CRect_i_((icon % 8) * 32, (icon >> 3) * 32, 0x20, 0x20), (int) (page->tex0 & 0x3FFF), (int) ((page->tex0 >> 14) & 0x3F),
                  (int) ((page->tex0 >> 20) & 0x3F), x, y, 0);
}

// P2's member's weapon, or its stand-in's when it holds none.
int PartnerWeaponItem(int chara) {
    const WEAPON_HAVE *weapon = EquippedWeapon(chara);

    if (weapon != nullptr) {
        return weapon->item_no;
    }

    return StandInWeapon(chara).item_no > 0 ? StandInWeapon(chara).item_no : -1;
}

// Copies P2's member's weapon pictures where DngActiveWeaponTextureCopy puts the controlled
// character's (its weapon, then its default weapons); returns the weapon, or -1 when nothing could be
// copied.
int CopyPartnerWeaponIcon(int chara) {
    CTexture *page = TexManager.GetTexture((char *) "itempack", -1);
    CTexture *sheet = IconSheet();
    int       item = PartnerWeaponItem(chara);

    // Index pictures of one format only: the copy moves indices, not colours.
    if (page == nullptr || sheet == nullptr || item <= 0 || ((sheet->tex0 >> 20) & 0x3F) != ((page->tex0 >> 20) & 0x3F)) {
        return -1;
    }

    if (!Mutated("weapon_icon")) {
        int default_no = GetDefaultWeaponNo(chara);

        CopyIcon(sheet, page, item, 0, 0);
        CopyIcon(sheet, page, default_no + 1, 0, 32);
        CopyIcon(sheet, page, default_no, 32, 32);
    }

    return item;
}

} // namespace

// coop-speedicon test hook (DC_COOP_SPEEDICON=<dir>): whose weapon picture the speed gauge has (the mod's for P2's partner,
// the game's for its player) and who that is; once a kind has held 200 frames, three of its frames, 60 apart, go to
// <dir>/speedicon-<frame>.png with a line naming the kind (w-town-apart -Case p2plays -P1Follows -SpeedIcon). A few frames
// only: each is a full-size PNG.
static void SpeedIconReport() {
    static const char *const dir = TestEnv("DC_COOP_SPEEDICON");
    static std::string       kind;
    static long              frame = 0, held = 0;
    static std::map<std::string, int> shots;

    if (dir == nullptr) {
        return;
    }

    const bool         mod = LocalIsPartner();
    const WEAPON_HAVE *own = EquippedWeapon(UserStatus->cur_chara);
    char               now[96];

    std::snprintf(now, sizeof now, "picture %s, member %d, weapon %d", mod ? "mod" : "game", mod ? g_partner.loaded_chara : (int) UserStatus->cur_chara,
                  mod ? PartnerWeaponItem(g_partner.loaded_chara) : (own != nullptr ? (int) own->item_no : -1));
    frame++;
    held = kind == now ? held + 1 : 0;
    kind = now;

    if (held < 200 || held % 60 != 0 || shots[kind] >= 3) {
        return;
    }

    char path[512];

    shots[kind]++;
    std::snprintf(path, sizeof path, "%s/speedicon-%06ld.png", dir, frame);
    NetplayTestShotTo(path);
    std::printf("coop-speedicon: shot %s, copy of p%d, %s\n", path, NetplayLocalPlayer() == 2 ? 2 : 1, now);
    std::fflush(stdout);
}

bool CoopDrawWeaponPanel(int alpha) {
    g_weapon_panel_shown.drawn = false;
    g_weapon_panel_shown.icon_item = -1;
    SpeedIconReport();

    // Only where this copy's own player is P2; in couch co-op the panel is the player's, and P2 has
    // its line under its life bar.
    if (!LocalIsPartner() || TEX_WepGage == nullptr) {
        return false;
    }

    int chara = g_partner.loaded_chara;
    int item = PartnerWeaponItem(chara);
    int gauge = PartnerGauge();
    int y = SCREEN_HEIGHT - 0x34;

    // The game's own panel (DunMainDraw's: the frame with the weapon picture, then the gauge), with
    // P2's gauge.
    if (gauge >= 100) {
        set2DSprite(Vif1Packet, TEX_WepGage, CRect_i_(0x28, y, 0x80, 0x1C), CRect_i_(0, 0, 0x80, 0x1C), alpha);
    } else {
        set2DSprite(Vif1Packet, TEX_WepGage, CRect_i_(0x28, y, 0x80, 0x1C), CRect_i_(0, 0x54, 0x80, 0x1C), alpha);
        set2DSprite(Vif1Packet, TEX_WepGage, CRect_i_(0x42, y, gauge, 0x1C), CRect_i_(0x1A, 0x38, gauge, 0x1C), alpha);
    }

    // The weapon picture the game draws beside it: P2's.
    int  copied = CopyPartnerWeaponIcon(chara);
    bool icon = copied >= 0 && !Mutated("weapon_icon");

    if (icon) {
        g_weapon_panel_shown.icon_item = copied;
    }

    g_weapon_panel_shown.drawn = true;
    g_weapon_panel_shown.item = item;
    g_weapon_panel_shown.gauge = gauge;
    g_weapon_panel_shown.icon = icon;
    g_weapon_panel_shown.icon_sheet = icon ? g_icon_source : 0;
    g_weapon_panel_shown.frames++;

    static int log_wait = 0;

    if (TestEnv("DC_COOP_DEBUG") != nullptr && --log_wait <= 0) {
        log_wait = 60;
        std::printf("coop-debug: p2 weapon panel chara %d (party %d, %s), item %d, icon item %d (sheet %d), top-left item %d, copies %d\n", chara,
                    (int) UserStatus->party_size, EquippedWeapon(chara) != nullptr ? "equipped" : "stand-in", item,
                    g_weapon_panel_shown.icon_item, g_icon_source, g_top_status_shown.item, StarterCopies(chara));
    }

    return true;
}

namespace coop {

TopStatusShown g_top_status_shown;

// coop-keydoor: DrawBlocksSnapshotApplied entered itemlst.img's sheets again at the table's keys after a join: the sheet this
// copy now holds is the HUD's own (not to be loaded again over the HUD's cells while their copy is under way).
void HudIconSheetEntered() {
    g_icon_source = 1;
}

} // namespace coop

// The game's top-left status for the player this copy plays: the controlled character's, or on the
// linked copy whose player is P2 (LocalIsPartner) P2's life, weapon line and water (its own, see
// StepThirst), topStatusInfo itself drawing them. For that one call the save's status holds P2 in the
// controlled character's place (cur_chara, and its life where it is a stand-in); all of it is put back
// before it returns, so nothing in the tick ever sees it.
void CoopTopStatusInfo(int y, int selected_item, int floor) {
    g_top_status_shown.drawn = false;

    if (!LocalIsPartner()) {
        topStatusInfo(y, selected_item, floor);
        return;
    }

    CUserStatus *status = UserStatus;
    int          chara = g_partner.loaded_chara;
    s8           cur_chara = status->cur_chara;
    s16          hp = status->hp[chara];
    s16          max_hp = status->max_hp[chara];
    s8           slot = status->equipped_weapon_slot[chara];
    WEAPON_HAVE  slot0 = status->chara_weapons[chara][0];
    bool         armed = EquippedWeapon(chara) != nullptr;

    status->cur_chara = (s8) chara;
    status->hp[chara] = (s16) Life();
    status->max_hp[chara] = (s16) MaxLife();

    // A stand-in with no weapon shows its starting weapon's line.
    if (!armed) {
        status->chara_weapons[chara][0] = StandInWeapon(chara);
        status->equipped_weapon_slot[chara] = 0;
    }

    const WEAPON_HAVE &weapon = status->chara_weapons[chara][(int) status->equipped_weapon_slot[chara]];

    g_top_status_shown.drawn = true;
    g_top_status_shown.chara = chara;
    g_top_status_shown.life = status->hp[chara];
    g_top_status_shown.life_max = status->max_hp[chara];
    g_top_status_shown.whp = (int) weapon.durability_f;
    g_top_status_shown.whp_max = weapon.durability;
    g_top_status_shown.item = weapon.item_no;
    g_top_status_shown.water = status->water_now[chara];
    topStatusInfo(y, selected_item, floor);

    status->cur_chara = cur_chara;
    status->hp[chara] = hp;
    status->max_hp[chara] = max_hp;
    status->equipped_weapon_slot[chara] = slot;
    status->chara_weapons[chara][0] = slot0;
}

void CoopStatusErrDraw(int y) {
    int chara = g_partner.loaded_chara;
    s8  cur_chara = UserStatus->cur_chara;
    int whose = LocalIsPartner() && chara >= 0 && chara < 6 && !Mutated("errdraw_player") ? chara : cur_chara;

    UserStatus->cur_chara = (s8) whose;
    BtStatusErrDraw(y);
    UserStatus->cur_chara = cur_chara;

    // Test hook (DC_COOP_STATUSICONS): whose ailments the corner icon showed, on which copy.
    if (static int wait = 0; TestEnv("DC_COOP_STATUSICONS") != nullptr && --wait <= 0) {
        wait = 30;
        std::fprintf(stderr, "coop-p2hurt: corner icon for member %d ailments %#x, copy of p%d\n", whose, (unsigned) UserStatus->ailments[whose],
                     NetplayLocalPlayer() == 2 ? 2 : 1);
    }
}

// The game's character wheel (Select; CharaChangeDraw calls this for each portrait): a "1P" or "2P" on the member
// each player plays (Jep, 2026-10-21). Draw only. DC_COOP_MUTATE=wheel_marks_off: no marks (twin of v-wheel-marks).
void CoopCharaWheelMark(int chara, float x, float y, int alpha) {
    static int last_frame = -100;
    static int frame = 0;

    static int open_seen = -100;

    if (!Enabled() || UserStatus == nullptr) {
        return;
    }
    if (frame - open_seen > 60) {
        std::printf("coop-wheel: the character wheel is open\n");
    }
    open_seen = ++frame;
    if (Mutated("wheel_marks_off")) {
        return;
    }

    const char *mark = chara == UserStatus->cur_chara                         ? "1P"
                       : g_partner.visible && chara == g_partner.loaded_chara ? "2P"
                                                                              : nullptr;

    if (mark == nullptr) {
        return;
    }

    dc::text::caption::LineStyle style;

    // The game's own lettering (its message font), not the status font (Jep, 2026-10-24).
    style.status_font = false;
    style.edge_width = 1.5f;

    if (mark[0] == '2') {
        style.fill[0] = 120;
        style.fill[1] = 200;
    }

    dc::text::caption::DrawLine(mark, x + 12.0f, y + 40.0f, 13.0f, 1.0f, 40.0f, style, (float) alpha / 128.0f, 1.0f, nullptr);

    // Once per opening, each mark: what it marked.
    static int last[2] = {-100, -100};
    int        who = mark[0] - '1';

    (void) last_frame;
    if (++frame - last[who] > 60) {
        std::printf("coop-wheel: the character wheel marks %s on member %d\n", mark, chara);
    }
    last[who] = frame;
}

// coop: gaps2
// The mini-map's second player: where the partner stands and the way it faces, while it is on the
// floor, for the arrow (marker on the aerial map) the map draws for it beside the player's; and whether
// this copy's own player is the partner (a linked copy B), so a map that follows its player follows it.
// Draw only. DC_COOP_MUTATE=minimap: the player alone, as before.
bool CoopMiniMapPartner(float *pos, float *yaw, bool *own) {
    if (!Enabled() || !g_partner.visible || g_partner.loaded_chara < 0 || g_partner.chara == nullptr || Mutated("minimap")) {
        return false;
    }

    sceVu0CopyVector(pos, g_partner.pos);
    *yaw = atan2f(g_partner.heading[0], g_partner.heading[2]);
    *own = NetplayLocalPlayer() == 2;

    static int log_wait = 0;

    if (TestEnv("DC_COOP_DEBUG") != nullptr && --log_wait <= 0) {
        log_wait = 60;
        std::printf("coop-debug: minimap marks p2 at (%.0f %.0f)%s\n", pos[0], pos[2], *own ? ", its own" : "");
    }

    return true;
}

// coop: minimap
// Teal: retail's pink arrow (about 244 142 226) comes out near 30 255 255, far from it and from the
// monsters' red dots. Draw only, the same on both copies.
bool CoopMiniMapOtherColour(unsigned char *red, unsigned char *green, unsigned char *blue) {
    if (!Enabled() || Mutated("maparrows")) {
        return false;
    }

    *red = 0x10;
    *green = 0xFF;
    *blue = 0xC0;
    return true;
}

// coop-uinotice (Jep's 2026-10-30 playtest, item 18: fixed colours per player on both screens): player 1's arrow keeps retail's
// pink and player 2's is teal (CoopMiniMapOtherColour's) over its dot, on either copy, so each player is one colour to both.
// other: the arrow is the other copy's player's. True when the arrow takes the teal (and r, g, b are set). DC_COOP_MUTATE=
// arrows_by_viewer (twin): the other copy's arrow teal, each copy's own pink, as before.
bool CoopMiniMapArrowColour(bool other, unsigned char *red, unsigned char *green, unsigned char *blue) {
    if (Mutated("arrows_by_viewer")) {
        return other && CoopMiniMapOtherColour(red, green, blue);
    }

    const int local = std::max(1, NetplayLocalPlayer()); // (unlinked: 0, player 1's screen)
    const int player = other ? 3 - local : local;

    return player == 2 && CoopMiniMapOtherColour(red, green, blue);
}

// coop-minimapfill
int CoopMiniMapFillAlpha(int alpha) {
    return Mutated("mapfill") ? alpha / 4 : alpha;
}

void CoopMiniMapTiles(int alpha, const int *places, int count) {
    static const bool log = TestEnv("DC_COOP_MAPTILES") != nullptr;

    if (!log || !Enabled()) {
        return;
    }

    std::string list;

    for (int i = 0; i < count; i++) {
        list += " " + std::to_string(places[i * 2]) + "," + std::to_string(places[i * 2 + 1]);
    }

    std::printf("coop-debug: minimap tiles, alpha %d, seen %d:%s, copy of p%d\n", alpha, count, list.c_str(),
                NetplayLocalPlayer() == 2 ? 2 : 1);
}
void CoopMiniMapArrows(int own_x, int own_y, int other_x, int other_y) {
    static const bool log = TestEnv("DC_COOP_MAPARROWS") != nullptr;

    if (log && Enabled()) {
        // coop-uinotice: and which arrow took the teal (CoopMiniMapArrowColour, as DrawArrow asks it), so the test reads the
        // colour each player got from the code itself, not only from a frame that may come from another tick under load.
        unsigned char r = 0, g = 0, bl = 0;
        const bool    own_teal = CoopMiniMapArrowColour(false, &r, &g, &bl);
        const bool    other_teal = CoopMiniMapArrowColour(true, &r, &g, &bl);
        std::printf("coop-debug: minimap arrows own (%d %d), other (%d %d), copy of p%d, own %s, other %s\n", own_x, own_y, other_x,
                    other_y, std::max(1, NetplayLocalPlayer()), own_teal ? "teal" : "pink", other_teal ? "teal" : "pink");
    }
}
