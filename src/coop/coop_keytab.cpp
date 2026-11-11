#include "coop/game_ns.hpp"
#include "coop/coop_keytab.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "battlemenu.hpp"
#include "itemdata.hpp"
#include "localize/localize.hpp"
#include "menu_draw.hpp"
#include "rect.hpp"
#include "texture.hpp"
#include "ui/framework/text.hpp"
#include "userstatus.hpp"

#include "coop/coop.hpp"
#include "coop/coop_camera.hpp" // DrawRngEnter: the label's layout off the lockstep's random numbers
#include "coop/coop_core.hpp"
#include "coop/coop_inventory.hpp"
#include "coop/netplay.hpp"

namespace dc::game {
extern PERSONAL_BOARD *PerBoardPt; // menu_draw.cpp: the board being stepped or drawn
} // namespace dc::game

namespace coop {

namespace {

// The pages in tab order.
constexpr int kPages[4] = {BOARD_PAGE_ITEM, BOARD_PAGE_WEAPON, BOARD_PAGE_ATTACH, kKeysTabPage};

// perbrd's tab strip (PersonalBoardTagDraw): row k (texel y k * 0x30) is the board's top band with tab k on it, at texel
// x 0x100 + 0x0F + k * 0x41, 0x42 wide at its foot; the tab's rows are the strip's first 0x1E, the band its last 0x12,
// with the tab's icon's shadow on the band under it.
constexpr int kStripX = 0x100;
constexpr int kTabFoot = 0x0F;
constexpr int kTabPitch = 0x41;
constexpr int kTabW = 0x42;
constexpr int kTabH = 0x1E;
constexpr int kBandH = 0x12;
// Four tabs on the band: 0x35 apart, from retail's first tab's place, so the last one's foot ends at 0xF0, as far in from
// the band's rounded right end (0xF2 on its top row) as the first one's is from its left (0x0D): centred over the board,
// and no tab's foot runs onto a corner of the band.
constexpr int kStep = 0x35;
// The front tab's column: its rows down to its icon's shadow's last (0x2C); the band's bottom rows stay the band's own.
constexpr int kFrontH = 0x2D;
// The count bubble's move off the fourth tab: still left of the screen's edge (x 0x154 + 0xD2 + 0x28 + 0x30 = 638).
constexpr int kBubbleShift = 0x28;

// The Attachments tab with its chest covered: the chest's pixels on each of the tab's rows (strip row 2, from the tab's
// foot: the first and one past the last that are the chest's red-brown or its black outline, a pixel either side, and on
// out over the outline's grey edge pixels up to plain stone), so the tab's own bevel and edge round it stay retail's. Each
// row is covered with one row of the band's body under the Weapons tab (strip row 0, x 0x50 on: no icon or shadow there;
// rows 0x2B and 0x2C, whose neighbours the texture's filter reads are the same stone), the stone the tab's inside is.
constexpr int kChestTop = 4;
constexpr unsigned char kChest[][2] = {
    {0x29, 0x2F}, {0x22, 0x30}, {0x18, 0x30}, {0x13, 0x30}, {0x11, 0x2F}, {0x11, 0x2D}, {0x11, 0x2C}, {0x11, 0x2B}, // 04-0B
    {0x11, 0x2A}, {0x11, 0x2A}, {0x11, 0x2B}, {0x11, 0x2C}, {0x11, 0x2D}, {0x11, 0x2E}, {0x12, 0x2F}, {0x12, 0x30}, // 0C-13
    {0x12, 0x31}, {0x12, 0x31}, {0x12, 0x31}, {0x13, 0x31}, {0x13, 0x31}, {0x13, 0x31}, {0x14, 0x31}, {0x14, 0x30}, // 14-1B
    {0x14, 0x2F}, {0x15, 0x2C}, {0x15, 0x2A}, {0x15, 0x27}, {0x16, 0x25}, {0x16, 0x21}, {0x16, 0x20}, {0x17, 0x1C}, // 1C-23
};
constexpr int kStoneX = kStripX + 0x50;
constexpr int kStoneY = 0x2B;

int IndexOf(int page) {
    for (int i = 0; i < 4; i++) {
        if (kPages[i] == page) {
            return i;
        }
    }

    return 0;
}

int KeyAt(const ITEM_PACK &ring, int cell) {
    int found = 0;

    for (int i = 0; i < ring.num && i < 103; i++) {
        const int item = ring.item[i];

        if (item >= ITEM_DUNGEON_START && (SharedItem(item) || FloorKeyItem(item))) {
            if (found++ == cell) {
                return item;
            }
        }
    }

    return cell < 0 ? found : -1;
}

// The fourth tab's key: the Silver Key's icon (DC_COOP_KEYTAB_ICON=<item> for a capture of another).
int TabIcon() {
    static const char *pick = TestEnv("DC_COOP_KEYTAB_ICON");

    return pick != nullptr ? std::atoi(pick) : ITEM_SILVER_KEY;
}

// The fourth tab at its foot's x (left) and the strip's top (top): the Attachments tab's slice, all of its rows (rows,
// kFrontH for the front tab: its foot into the band and its icon's shadow; kTabH behind, the band drawn over), the chest
// covered with stone and the key's icon where the chest stood.
// bright: 0x80, or the weapon Attach board's 0x40 for a tab behind (BoardBright).
void DrawKeysTab(int left, int top, int rows, CTexture *texture, int alpha, int bright) {
    DrawMenu2DSprite(texture, CRect_i_(left, top, kTabW, rows), CRect_i_(kStripX + kTabFoot + 2 * kTabPitch, 2 * 0x30, kTabW, rows), bright, bright,
                     bright, alpha);

    for (int i = 0; i < (int) (sizeof(kChest) / sizeof(kChest[0])) && kChestTop + i < rows; i++) {
        const int from = kChest[i][0];
        const int width = kChest[i][1] - from;

        DrawMenu2DSprite(texture, CRect_i_(left + from, top + kChestTop + i, width, 1), CRect_i_(kStoneX + from, kStoneY + i % 2, width, 1), bright,
                         bright, bright, alpha);
    }

    // The key as retail's tab icons are drawn: the sprite alone (DrawIconParts adds a grid cell's drop shadow, which the tabs'
    // baked icons have none of).
    int       u = 0;
    int       v = 0;
    CTexture *icon = RetCTex(TabIcon(), u, v);

    MenuTextureReload(ItemMenuWeaponIconReadBlock);
    if (icon != nullptr) {
        DrawMenu2DSprite(icon, CRect_i_(left + kTabW / 2 - 0x10, top + 3, 32, 32), CRect_i_(u, v, 32, 32), bright, bright, bright, alpha);
    }
    MenuTextureReload(texture->block);
}

// The KEYS label: the ITEM label's yellow plate (its ends, 8 wide, cut a pixel clear of the I and the M so the texture's
// filter reads no letter at the seams, and a column with no letter stretched between them) round the word in the game's font, centred over the fourth tab and kept left of the R1 mark as ATTACHMENT's is.
void DrawKeysLabel(int x, int y, CTexture *texture, int alpha) {
    DrawRngEnter(); // coop-drawrng: the label's layout rolls no lockstep number
    dc::ui::Label label;

    label.KeepGameRandom(true);
    label.SetText(dc::localize::LocalizeText("coop.bag.keys_tab", "KEYS"));
    label.SetDropShadow(false);
    label.SetCustomRgb(24, 20, 12);
    // DC_COOP_KEYTAB_FONT=scale,xscale,weight (0 thin, 1 normal, 2 bold): a capture's trial of another look
    static const char *font = TestEnv("DC_COOP_KEYTAB_FONT");
    float scale = 1.1f, xscale = 0.6f;
    int   weight = 0;
    if (font != nullptr) {
        std::sscanf(font, "%f,%f,%d", &scale, &xscale, &weight);
    }
    label.SetScale(scale);
    label.SetXScale(xscale);
    label.SetWeight(static_cast<dc::ui::FontWeight>(weight));
    const int text_w = label.GetWidth();
    const int plate_w = 8 + text_w + 8;
    const int centre = x + kTabFoot + 3 * kStep + kTabW / 2 - 4;
    const int left = std::min(centre - plate_w / 2, x + 0xD0 - plate_w);
    const int top = y - 0x4C + 1;

    DrawMenu2DSprite(texture, CRect_i_(left, top, 8, 0x1B), CRect_i_(0, 0xC0, 8, 0x1C), alpha);
    DrawMenu2DSprite(texture, CRect_i_(left + 8, top, text_w, 0x1B), CRect_i_(0x16, 0xC0, 1, 0x1C), alpha);
    DrawMenu2DSprite(texture, CRect_i_(left + 8 + text_w, top, 8, 0x1B), CRect_i_(0x2B, 0xC0, 8, 0x1C), alpha);
    label.Draw(left + 8, top + 1, alpha);
    DrawRngLeave();
    MenuTextureReload(texture->block);
}

// coop-attachhelp (Jep's pack 8 playtest, 11-04: "in the attachment window you can't see the key slot, which should lack
// highlighting like the other tabs"): the weapon menu's Attach board draws the item board's tabs as retail does, the ones
// behind dimmed (PersonalBoardTagDraw's 0x40), so with the Keys tab on the item board it shows there too, dimmed, the
// Attachments tab in front. Only the look: the board has no Keys page (no paging, cells or pointer tab).
// DC_COOP_MUTATE=keytab_weaponboard: retail's three tabs there (twin).
bool WeaponBoardTabs() {
    return Enabled() && !Mutated("keytab") && !Mutated("keytab_weaponboard") && PerBoardPt == &WepMenu.board &&
           PerBoardPt->menu_kind == PERSONAL_BOARD_WEAPON_ATTACH;
}

bool TabsShown() {
    return KeysTabOn() || WeaponBoardTabs();
}

// The tabs behind the front one: dimmed on the weapon Attach board, as retail draws its three.
int BoardBright() {
    return PerBoardPt->menu_kind == PERSONAL_BOARD_WEAPON_ATTACH ? 0x40 : 0x80;
}

} // namespace

bool KeysTabOn() {
    return Enabled() && !Mutated("keytab") && PerBoardPt == &ItemMenuMode.board && PerBoardPt->menu_kind == PERSONAL_BOARD_ITEM &&
           !ItemMenuMode.overflow;
}

bool KeysTabPage(int step) {
    if (!KeysTabOn()) {
        return false;
    }

    const int from = PerBoardPt->page;

    PerBoardPt->page = kPages[(IndexOf(from) + step + 4) % 4];
    std::fprintf(stderr, "coop-keytab: page %d -> %d (%s, %d keys; copy of player %d)\n", from, PerBoardPt->page, step < 0 ? "L1" : "R1",
                 KeysTabCount(), NetplayRole());
    return true;
}

int KeysTabCount() {
    return MenuItemPackPt != nullptr ? KeyAt(ShownRing(), -1) : 0;
}

int KeysTabCells() {
    return std::max(20, (KeysTabCount() + 4) / 5 * 5);
}

int KeysTabItemAt(int cell) {
    return MenuItemPackPt != nullptr && cell >= 0 ? KeyAt(ShownRing(), cell) : -1;
}

void KeysTabIcons(int x, int y, int top, int bottom, int alpha) {
    if (MenuItemPackPt == nullptr) {
        return;
    }

    const ITEM_PACK &ring = ShownRing();
    const int        count = KeyAt(ring, -1);

    for (int i = 0; i < count; i++) {
        DrawIconParts(KeyAt(ring, i), x + i % 5 * 0x28, y + i / 5 * 0x28, top, bottom, alpha, 0);
    }
}

bool KeysTabTagDraw(int tag, int x, int y, CTexture *texture, int alpha) {
    if (!TabsShown()) {
        return false;
    }

    const int front = IndexOf(tag);
    const int bright = BoardBright();

    if (PerBoardPt->menu_kind == PERSONAL_BOARD_WEAPON_ATTACH) {
        static bool said = false;

        COOP_TEST_TRACE_RESET(said, false);
        if (!said) { // once a job (a test's line)
            said = true;
            std::fprintf(stderr, "coop-keytab: weapon Attach board: four tabs, Keys dimmed (copy of player %d)\n", NetplayRole());
        }
    }
    const int top = y - 0x30 + 2;
    const int band = top + kTabH;

    // As retail (each strip row the band with one tab, the front row drawn last): the tabs behind, farthest from the front
    // first (a neighbour's foot overlaps); the band with no tab's foot or shadow on it (row 2's left part, row 0's right
    // part) over their feet; then the front tab's whole column, its foot running into the band and its icon's shadow on it.
    int order[4];
    int count = 0;

    for (int far = 3; far >= 1; far--) {
        for (int i = 0; i < 4; i++) {
            if (std::abs(i - front) == far) {
                order[count++] = i;
            }
        }
    }

    for (int n = 0; n < count; n++) {
        const int i = order[n];

        if (i == 3) {
            DrawKeysTab(x + KeysTabLeft(i), top, kTabH, texture, alpha, bright);
        } else {
            DrawMenu2DSprite(texture, CRect_i_(x + KeysTabLeft(i), top, kTabW, kTabH), CRect_i_(kStripX + kTabFoot + i * kTabPitch, i * 0x30, kTabW, kTabH),
                             bright, bright, bright, alpha);
        }
    }

    DrawMenu2DSprite(texture, CRect_i_(x, band, 0x90, kBandH), CRect_i_(kStripX, 2 * 0x30 + kTabH, 0x90, kBandH), alpha);
    DrawMenu2DSprite(texture, CRect_i_(x + 0x90, band, 0x70, kBandH), CRect_i_(kStripX + 0x90, kTabH, 0x70, kBandH), alpha);

    if (front == 3) {
        DrawKeysTab(x + KeysTabLeft(front), top, kFrontH, texture, alpha, 0x80);
    } else {
        DrawMenu2DSprite(texture, CRect_i_(x + KeysTabLeft(front), top, kTabW, kFrontH), CRect_i_(kStripX + kTabFoot + front * kTabPitch, front * 0x30, kTabW, kFrontH),
                         alpha);
    }

    // The front tab's label: retail's sprite moved with its tab, or KEYS.
    if (front == 3) {
        DrawKeysLabel(x, y, texture, alpha);
    } else {
        const int widths[3] = {0x34, 0x45, 0x4E};
        const int sources[3] = {0x00, 0x34, 0x79};
        const int offsets[3] = {2, -8, -16};
        const int left = x + 0x12 + front * 0x44 + offsets[front] + KeysTabLeft(front) - (kTabFoot + front * kTabPitch);

        DrawMenu2DSprite(texture, CRect_i_(left, y - 0x4C + 1, widths[front], 0x1B), CRect_i_(sources[front], 0xC0, widths[front], 0x1C), alpha);
    }

    return true;
}

int KeysTabBubbleShift() {
    return TabsShown() ? kBubbleShift : 0;
}

int KeysTabTabs() {
    return KeysTabOn() ? 4 : 3;
}

int KeysTabPageOf(int tab) {
    return kPages[std::clamp(tab, 0, 3)];
}

int KeysTabLeft(int tab) {
    return kTabFoot + tab * kStep;
}

int KeysTabWidth() {
    return kStep;
}

int KeysTabSavedPage(int page) {
    return page == kKeysTabPage ? BOARD_PAGE_ITEM : page;
}

} // namespace coop
