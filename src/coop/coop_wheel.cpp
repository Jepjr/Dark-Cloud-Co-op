#include "coop/game_ns.hpp"
#include "coop/coop.hpp"
#include "coop/coop_audio.hpp" // coop-menuse
#include "coop/coop_wheel.hpp"

#include <libvu0.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "btactstatus.hpp"
#include "text/caption_text.hpp"
#include "character.hpp"
#include "common.h"
#include "dataread.hpp"
#include "dun/gameloop.hpp"
#include "app/gameloop.hpp" // coop-wheeltwin49: GameRequestStop
#include "gfx/gfx.hpp" // coop-usebands2: UiAnchorScope
#include "mglib.hpp"
#include "rect.hpp"
#include "snd.hpp"
#include "menu_draw.hpp"
#include "texture.hpp"
#include "userstatus.hpp"
#include "savedata.hpp" // coop-dpadwheel: SaveData (the party in town)
#include "editmapscript.hpp" // coop-townrings: GameMode, ED_MODE_WALK
#include "main.hpp"          // coop-townrings: mode

namespace dc::game {
extern s32 gameTask;
void QuickChangeKeepLockOn(bool keep); // coop-p2controls: DunMoveChara's quick change lock-on, split out (movechara.cpp)
void QuickChangeLockOnBack();
} // namespace dc::game

void CoopCharaWheelMark(int chara, float x, float y, int alpha); // coop_hud.cpp: the 1P/2P marks retail's ring draws (coop-swapmenulook)

#include "dun/gameloop.hpp"   // coop-p2controls: lockOnTargetFlag (the line his swap logs)
#include "itemdata.hpp"       // coop-p2controls: WEAPON_HAVE
#include "menu_dungeon.hpp"   // coop-p2controls: the element picker's rules and icons
#include "weaponelement.hpp"  // coop-p2controls: WEAPON_ELEMENT_NONE

#include "coop/coop_combat.hpp" // coop-p2controls: PartnerSwapLock, PartnerFirstPerson
#include "coop/coop_actor.hpp"   // coop-townrings: kActorMenuTown
#include "coop/coop_core.hpp"
#include "coop/coop_opener.hpp" // coop-p2controls3: PlayerPresenting
#include "coop/coop_fullmenu.hpp" // coop-wheelcrash
#include "coop/coop_interact.hpp" // coop-keypick
#include "coop/coop_party.hpp"
#include "coop/coop_pause.hpp" // coop-pausewheel
#include "coop/coop_prompt.hpp"
#include "coop/coop_town.hpp"
#include "coop/coop_townmenu.hpp" // coop-townrings: his and her town menus and her pause
#include "coop/coop_georama.hpp"  // coop-townrings: CoopP2Building
#include "coop/coop_members.hpp"  // coop-townrings: Member, SetMember
#include "coop/coop_worlds.hpp" // coop-wheelcrash
#include "coop/coop_zonechara.hpp" // coop-pt1111k
#include "coop/netplay.hpp"

bool CoopTestGamePicker(); // coop-p2controls3 (defined below, outside the namespace: movechara.cpp reads it)

namespace coop {

namespace {

// Ticks a turn of the ring takes to show (drawing only reads it). coop-swapmenulook: retail's 20 (CharaChangeKey's Turning).
// DC_COOP_MUTATE=ringlook (twin): 6, as before.
int TurnTicks() {
    return Mutated("ringlook") ? 6 : 20;
}

constexpr std::uint16_t kTurnNext = kInputRight | kInputDown;
constexpr std::uint16_t kTurnBack = kInputLeft | kInputUp;

struct Wheel {
    bool          open = false;
    int           members[6] = {};
    int           count = 0;
    int           selected = 0;
    int           turn = 0;
    int           turn_direction = 0;
    std::uint16_t held = 0;
    bool          town = false; // opened in town (coop_town's partner and rules)
    // coop-wheelcrash: Select opens it on its release with nothing else pressed meanwhile (pad 2's Select+L3 and
    // Select+Square are hers already).
    bool          select_clean = false;
    // Player 1's: the buttons the game must not see (all while open; after it closes, those still held). coop-georamaring: player 2's
    // too, the buttons her Georama must not see (RingHold).
    std::uint16_t mask = 0;
    std::int64_t  stepped = -10; // the link tick it last stepped on
    int           ticks = 0;
    float         other_at[3] = {}; // the other player's place as it opened (the world goes on meanwhile)
    int           opens = 0; // how many times it opened (the draw's log, which is this copy's own, keys on it)
    int           picks = 0; // coop-swapmenulook: how many picks closed it (the draw plays retail's leaving on each)
    // coop-p2controls: the element ring (her D-pad Up with her Element Quick Select on, retail's quick select): members are the
    // elements retail's picker offers for the weapon of chara (menu_dungeon.cpp ElementChoicesOf), its own selected first.
    bool          elements = false;
    int           chara = -1;
};

// coop-wheelcrash: one each, [0] player 1's (linked only: his Select, replacing retail's wheel there), [1] pad 2's.
Wheel  g_wheels[2];
Wheel &g_wheel = g_wheels[1];
long long g_town_let_go = -1; // coop-menucross: the link tick her town wheel last closed

// The faces: quickchr.img (the player's wheel's own pictures, "quickchara", six 48 by 48 faces) in a
// texture block of the partner's own, never the player's wheel's 0x17 nor P2's effect's 0x42. Loaded
// again when the wheel next opens after anything has emptied it.
std::vector<u_char> g_faces_file;

// The effectblock test's twin puts the faces back in P2's effect block (0x42), as they were.
int WheelBlock() {
    return Mutated("wheel_block") ? 0x42 : kWheelTextureBlock;
}

// coop-ringlog: what the element sheet (wepicon in block 0x44) was last made or moved by, and when: every open of an element ring logs it
// beside what the sheet's cells hold now (a cell nobody painted shows as no texels).
struct SheetHistory {
    char      what[40] = "never";
    long long tick = -1;
    int       times = 0;
    int       bank_n = -2; // the none cell as the bank read last held it (what the painter left in quickchr.img's wepicon): texels drawn, -2 never read
    int       bank_m = 0;
};

SheetHistory g_sheet;

// The picture's pixel (x, y) of the wepicon sheet as colour and alpha, from the staged pixels the draw reads; false when they cannot be read.
bool SheetTexel(const u_char *pixels, const u_char *clut, int bytes, bool swizzled, int x, int y, int *grey, int *alpha) {
    constexpr int kWidth = 256;

    if (pixels == nullptr) {
        return false;
    }
    if (bytes == 4) { // a baked colour picture (PSMCT32): R, G, B, A bytes
        const u_char *c = pixels + (y * kWidth + x) * 4;

        *grey = (c[0] * 299 + c[1] * 587 + c[2] * 114) / 1000;
        *alpha = c[3];
        return true;
    }
    if (clut == nullptr) {
        return false;
    }

    int offset = y * kWidth + x;

    if (swizzled) { // PSMT8 in the GS's block order, its CLUT in CSM1 order (menu_dungeon.cpp: Psmt8Offset, Csm1Index)
        const int block = (y & ~0xF) * kWidth + (x & ~0xF) * 2;
        const int swap = (((y + 2) >> 2) & 1) * 4;
        const int row = (((y & ~3) >> 1) + (y & 1)) & 7;

        offset = block + row * kWidth * 2 + ((x + swap) & 7) * 4 + ((y >> 1) & 1) + ((x >> 2) & 2);
    }

    int index = pixels[offset];

    if (swizzled) {
        index = (index & ~0x18) | ((index & 0x08) << 1) | ((index & 0x10) >> 1);
    }

    const u_char *c = clut + index * 4;

    *grey = (c[0] * 299 + c[1] * 587 + c[2] * 114) / 1000;
    *alpha = c[3];
    return true;
}

// The texels of a cell that draw (alpha above 0) and their mean grey; -1 texels when the sheet cannot be read.
void SheetCell(const u_char *pixels, const u_char *clut, int bytes, bool swizzled, int u, int v, int size, int *painted, int *mean) {
    long long sum = 0;

    *painted = 0;
    *mean = 0;
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            int grey = 0;
            int alpha = 0;

            if (!SheetTexel(pixels, clut, bytes, swizzled, u + x, v + y, &grey, &alpha)) {
                *painted = -1;
                return;
            }
            if (alpha > 0) {
                (*painted)++;
                sum += grey;
            }
        }
    }
    *mean = *painted > 0 ? (int) (sum / *painted) : 0;
}

// The none cell (cell 125) of the bank's own wepicon sheet (IM2 entry "wepicon": a TIM2 of 256 by 640 eight-bit texels, its CLUT after
// them, as menu_dungeon.cpp's FindIconSheet reads it), right after the painter ran: what the sheet is made of before any texture holds it.
void BankNoneCell(u_char *bank) {
    g_sheet.bank_n = -1;
    if (bank == nullptr || std::memcmp(bank, "IM2", 3) != 0) {
        return;
    }

    auto word = [](const u_char *d) { return (unsigned) d[0] | ((unsigned) d[1] << 8) | ((unsigned) d[2] << 16) | ((unsigned) d[3] << 24); };
    const unsigned count = word(bank + 4);

    for (unsigned i = 0; i < count && i < 64; i++) {
        const u_char *entry = bank + 0x10 + i * 0x30;

        if (std::strncmp(reinterpret_cast<const char *>(entry), "wepicon", 32) != 0) {
            continue;
        }

        const u_char *tim = bank + word(entry + 0x20);
        const u_char *picture = tim + 0x10;

        if (std::memcmp(tim, "TIM2", 4) != 0) {
            return;
        }

        const u_char *pixels = picture + (picture[0x0C] | (picture[0x0D] << 8));
        int           u = 0, v = 0, cell = 0;

        ElementIconCell(WEAPON_ELEMENT_NONE, &u, &v, &cell);
        SheetCell(pixels, pixels + 256 * 640, 1, true, u, v, cell, &g_sheet.bank_n, &g_sheet.bank_m);
        return;
    }
}

} // namespace

// coop-ringlog: the places that read, load or re-key the sheet say so (the element ring's open line names the last).
void CoopWheelSheetNote(const char *what) {
    std::snprintf(g_sheet.what, sizeof g_sheet.what, "%s", what);
    g_sheet.tick = (long long) NetplayLinkTick();
    g_sheet.times++;
}

// coop-p2controls: the bank retail's quick change loads (menu_dungeon.cpp: the menu language's quickchr.pac, its quickchr.img:
// "quickchara", the wheel's faces, and "wepicon", the element picker's icons, its none cell made as the picker makes it), read
// into the ring's own buffer; null when it will not load. It was commenu/quickchr.img (the faces and the item lists, no wepicon).
unsigned char *CoopWheelBank() {
    constexpr int kBytes = 320 * 1024;

    if (g_faces_file.empty()) {
        g_faces_file.resize(kBytes + 64);
    }

    u_char *data = g_faces_file.data() + ((64 - reinterpret_cast<std::uintptr_t>(g_faces_file.data()) % 64) % 64);
    char    name[] = "quickchr.pac";
    char    img[] = "quickchr.img";
    int     size = LoadFileMenuData(name, reinterpret_cast<unsigned int *>(data));
    u_char *bank = size > 0 && size <= kBytes ? reinterpret_cast<u_char *>(GetPackFile(reinterpret_cast<u_int *>(data), img, nullptr)) : nullptr;

    if (bank == nullptr) {
        std::fprintf(stderr, "coop: cannot load quickchr.pac's quickchr.img (%d bytes read)\n", size);
        return nullptr;
    }
    ElementIconsMakeNone(bank);
    BankNoneCell(bank);
    CoopWheelSheetNote("bank read, none cell painted");
    return bank;
}

namespace {

void LoadFaces() {
    if (TexManager.GetTexture((char *) "quickchara", WheelBlock()) != nullptr) {
        return;
    }

    u_char *data = CoopWheelBank();

    if (data == nullptr) {
        return;
    }

    LOADTEXTURE_INFO2 table[2] = {
        {(char *) data, WheelBlock(), 0},
        {nullptr,       0,                  0},
    };

    TexManager.DeleteTextureBlock(WheelBlock());
    TexManager.CleanUpTextureList();
    TexManager.LoadTextureBlockEX(WheelBlock(), table);
    CoopWheelSheetNote("load (ring opened on an empty block)");
}

// The ring: pad 2's member at the top, then the others it may take, in party order after it.
// Jep (2026-10-21): there is no R3 menu. R3 opens nothing; the wheel stays for the bot tests that drive it
// (DC_COOP_TEST set) and for DC_COOP_MUTATE=r3menu, the twin of v-no-r3-menu. Logs "coop-wheel: pad 2 R3, no menu".
bool R3Menu() {
    return Mutated("r3menu") || TestEnv("DC_COOP_TEST") != nullptr;
}

// Player 1's ring (coop-wheelcrash): his member at the top, then the joined members alive with a weapon of their own;
// the partner's member too, which asks her to swap (coop_prompt's same-character rule).
bool PlayerPickable(int chara) {
    int slot = chara >= 0 && chara < 6 ? UserStatus->equipped_weapon_slot[chara] : -1;

    return chara >= 0 && chara < UserStatus->party_size && chara != UserStatus->cur_chara && UserStatus->hp[chara] > 0 && slot >= 0 && slot < 11 &&
           UserStatus->chara_weapons[chara][slot].item_no > 0;
}

// coop-pt1111k: the members player 1's ring offers: his own at the top, the partner's (a trade, asked), and the joined ones he
// may take; on a character-limited floor not one that would leave its member unplayed (ZonePickLegal, the offer's check:
// the pick is checked again as it is taken, WheelConfirm).
// coop-swapmenulook (Jep: the ring must look like the game's own): the whole party, in party order from his own, as retail's
// ring (StartQuickChange: party_size faces); one he may not take is refused as it is picked (WheelTakes), a fallen one drawn
// dark as retail draws it. DC_COOP_MUTATE=ringlook: the members he may take alone, as before.
int PlayerMembers(int *out) {
    int loaded = UserStatus->cur_chara;
    int count = 0;

    out[count++] = loaded;
    for (int k = 1; k < 6; k++) {
        int chara = (loaded + k) % 6;

        if (!Mutated("ringlook")) {
            if (chara < UserStatus->party_size && !(chara == g_partner.loaded_chara && Mutated("wheelask"))) {
                out[count++] = chara;
            }
            continue;
        }
        if (chara == g_partner.loaded_chara ? chara < UserStatus->party_size && !Mutated("wheelask") : PlayerPickable(chara)) {
            if (chara != g_partner.loaded_chara && !ZonePickLegal(0, chara, false)) {
                continue;
            }
            out[count++] = chara;
        }
    }
    return count;
}

// coop-pt1111k: Jep 11-11, an open ring follows the others' members (its own screen): when what it may offer changes, its
// entries change, the hand kept on the member it was on (else the top).
void Refresh(Wheel &w, const int *members, int count, int player) {
    if (count == w.count && std::equal(members, members + count, w.members)) {
        return;
    }
    const int on = w.members[w.selected];

    std::copy(members, members + count, w.members);
    w.count = count;
    w.selected = 0;
    for (int i = 0; i < count; i++) {
        if (members[i] == on) {
            w.selected = i;
        }
    }
    std::fprintf(stderr, "coop-wheel: player %d's ring now offers %d members (the others' members changed) at link tick %lld\n", player, count,
                 (long long) NetplayLinkTick());
}

// coop-swapmenulook: whether a Cross on chosen in player index's party ring does anything (her own or his: closes; the other's
// member: asks; one they may take: takes). Else retail's refusal (CharaChangeKey: a fallen member): the refuse sound, the ring
// stays open. A character-limited floor's refusal stays WheelConfirm's (its message).
bool WheelTakes(int index, int chosen) {
    if (index == 0) {
        return chosen == UserStatus->cur_chara || chosen == g_partner.loaded_chara || PlayerPickable(chosen);
    }
    return chosen == g_partner.loaded_chara || chosen == UserStatus->cur_chara || Pickable(chosen);
}

void OpenPlayer() {
    Wheel &w = g_wheels[0];

    w.town = false;
    w.elements = false;
    w.count = PlayerMembers(w.members);

    if (w.count < 2) {
        std::fprintf(stderr, "coop-wheel: player 1's wheel has nobody else to offer\n");
        return;
    }

    w.open = true;
    w.selected = 0;
    w.turn = 0;
    w.ticks = 0;
    w.opens++;
    std::copy(g_partner.pos, g_partner.pos + 3, w.other_at);
    LoadFaces();
    SndSePlay(MENU_SOUND_CURSOR, -1, 0);
    std::fprintf(stderr, "coop-wheel: player 1's wheel opens (%d members) at link tick %lld, task %d\n", w.count, (long long) NetplayLinkTick(),
                 (int) gameTask);
}

// Pad 2's ring's members: her own at the top, then in town the members pad 2 may play there (never Toan); in a dungeon the
// player's member too (picking it asks the player to swap), and (coop-pt1111k) not one that would leave a character-limited
// floor's member unplayed.
// coop-swapmenulook: in a dungeon, the whole party as retail's ring shows it (Pickable's joined rule: a party of one has Toan and
// Xiao), taken or refused as it is picked (WheelTakes); DC_COOP_MUTATE=ringlook: the members she may take alone, as before.
// coop-dpadmenus (Jep's rc4 playtest, 11-18: "player 1's town ring has nobody else to offer", a party of two): in town each ring offered
// only the members nobody played, so with two members neither player's ring had anyone. The floor's ring (and the Allies page, in town
// too: TownAlliesApply) offers another player's member, and its pick asks them for a trade: the town rings now do the same. port is
// the ring's player (0-based). DC_COOP_MUTATE=townringtrade (twin): the free members alone, as before.
bool TownTrade(int port, int chara) {
    return !Mutated("townringtrade") && TownMemberHolder(chara, port) >= 0;
}

int PadMembers(int loaded, bool town, int *out) {
    int count = 0;

    out[count++] = loaded;
    for (int k = 1; k < 6; k++) {
        int chara = (loaded + k) % 6;

        if (!town && !Mutated("ringlook")) {
            const bool joined = chara < UserStatus->party_size || (UserStatus->party_size <= 1 && chara <= CHARA_XIAO);

            if (joined && !(chara == UserStatus->cur_chara && Mutated("wheelask"))) {
                out[count++] = chara;
            }
            continue;
        }
        if (town ? TownMemberPickable(chara) || TownTrade(1, chara) : Pickable(chara) || (chara == UserStatus->cur_chara && loaded < UserStatus->party_size && !Mutated("wheelask"))) {
            if (!town && chara != UserStatus->cur_chara && !ZonePickLegal(1, chara, false)) {
                continue;
            }
            out[count++] = chara;
        }
    }
    return count;
}

void Open(int loaded, bool town) {
    std::printf("coop-wheel: opened\n");
    g_wheel.count = 0;
    g_wheel.town = town;
    g_wheel.elements = false;
    g_wheel.ticks = 0;
    g_wheel.opens++;
    std::copy(CharaMain.pos, CharaMain.pos + 3, g_wheel.other_at);
    g_wheel.count = PadMembers(loaded, town, g_wheel.members);

    if (g_wheel.count < 2) {
        std::printf("coop: pad 2's wheel has nobody else to offer\n");
        return;
    }

    g_wheel.open = true;
    g_wheel.selected = 0;
    g_wheel.turn = 0;
    LoadFaces();
    SndSePlay(MENU_SOUND_CURSOR, -1, 0);
    std::printf("coop: pad 2 opens the wheel (%d members)\n", g_wheel.count);
    std::fprintf(stderr, "coop-wheel: player 2's wheel opens (%d members) at link tick %lld, task %d\n", g_wheel.count, (long long) NetplayLinkTick(),
                 (int) gameTask);
}

// Select's release with nothing else pressed while it was down (coop-wheelcrash). DC_COOP_MUTATE=p2select_off (twin of
// t-wheel -Case p2): her Select opens nothing, as before.
bool SelectOpens(Wheel &w, std::uint16_t buttons, std::uint16_t pressed, int player) {
    bool released = (w.held & kInputSelect) != 0 && (buttons & kInputSelect) == 0;

    if ((pressed & kInputSelect) != 0) {
        w.select_clean = true;
    } else if ((pressed & ~kInputSelect) != 0) {
        w.select_clean = false;
    }

    return released && w.select_clean && !(player == 2 && Mutated("p2select_off"));
}

// coop-dpadwheel (Jep 11-17: "D-pad Down opens the character wheel, the same ring Select opens; D-pad Up stays the element ring"):
// the player's D-pad Down press, with their own Character Quick Select on (game.chara_quick_select, on by default; off: the
// game's D-pad Down, which does nothing on a floor). Every copy alike (the agreed pads, each player's own option row).
bool DownOpens(std::uint16_t pressed, const InputPadState &pad, int player) {
    if ((pressed & kInputDown) == 0 || (pad.buttons & kInputSelect) != 0) {
        return false;
    }
    const bool on = CoopCharaQuickSelectOnFor(player);

    std::fprintf(stderr, "coop-wheel: player %d's D-pad Down read with their character quick select %s at link tick %lld\n", player,
                 on ? "on" : "off", (long long) NetplayLinkTick());
    return on;
}

// coop-pausewheel (Jep's pack 7 playtest: the ring opened while the game was paused): a player's own pause keeps his or
// her ring shut (and CloseWheelForPause closes it as the pause starts); the other player's pause leaves it alone.
// DC_COOP_MUTATE=pausewheel (twin): the pause is not looked at, as before.
bool OwnPause(int player) {
    return PausePlayer(player) && !Mutated("pausewheel");
}

// coop-keydoor1118 (tests/test_chests.py's trapped chest, red since coop-dpadwheel 45bcc66e): a player's own use prompt (a chest's
// or a door's message and its choices, the key picker's) keeps his or her character ring shut, as it keeps the element rings
// (RingFree). The prompt's cursor is the D-pad: since D-pad Down opens the ring, his Down on a chest's "Give up / Use a Key /
// Force it open" opened his ring over it, which took his next Down and Cross (a pick) and the prompt read one Down of two.
// DC_COOP_MUTATE=promptring (twin): the prompt is not looked at, as before.
bool OwnPrompt(int player) {
    return UsePromptOpen(player) && !Mutated("promptring");
}

// coop-dpadmenus (Jep's rc4 playtest, 11-18, both players: "hitting down on the D-pad was registering the character select wheel even when
// moving through an item select menu"): the one rule every ring's open reads: whether this player's buttons belong to a window or menu of
// theirs now. Retail's own menu state (the Triangle menu, first or second, and a town's menus and shops: the in-menu bits) and our own
// windows: their pause, their chest's or door's prompt, their presentation, their key picker (the game's Use Item window at a lock, a
// key door or an event slot, and the cave door's), a question naming them (the swap or revive question, a gate's, a ready check's or a
// vote's, which hold them), and in a town their talk, own event, fishing, Georama or message (CoopTownPlayerBusy). Each is alike on every
// copy (agreed pads, commands on one link tick), so a ring opens on the same tick everywhere. Cause: the rings each listed some of
// these (DCO-200 added the prompt alone) and none the key picker: its cursor is the D-pad (Up and Down a row of five), so a Down there
// opened the character ring over the window (and Up the element ring), which took the next Cross. Every player alike (by number).
// DC_COOP_MUTATE=dpadmenus (twin): only the checks the rings had before (the menu, the pause, the prompt, player 1's question).
int TownApartSelf(); // (coop-apartrings, below)

bool InputInWindow(int player) {
    if (FullMenuOpenFor(player) || OwnPause(player) || OwnPrompt(player) || PromptAsksPartner()) {
        return true;
    }
    if (Mutated("dpadmenus")) {
        return false;
    }
    if (PlayerInMenu(player) || PlayerPresenting(player) || UsePromptOpen(player) || KeyPickOpenFor(player) || PromptQuestionOf(player) ||
        (!CoopWorldsApart() && CoopWorldsQuestionHolds(player - 1))) {
        return true;
    }
    if (mode != GAME_MODE_EDIT) {
        return false;
    }
    // (in a building the game's player walks in the room's own mode, ED_IN_MODE_WALK, under the town's GameMode 12, which
    // CoopTownPlayerBusy reads as a menu: there their talk, menu or message; TownRingPlace has their walk)
    if (EdInteriorFlag != 0 && TownApartSelf() > 0) {
        return false; // coop-apartrings: apart in a building, the copy's own player alone: its room walk (TownRingPlace) is the rule
    }
    if (EdInteriorFlag != 0 && player == 1) {
        return CoopTalkOn(0) || PlayerInMenu(1) || CoopSysMesHolds(1);
    }
    return CoopTownPlayerBusy(player - 1) != nullptr;
}

// The other player's walk while the wheel was open (the world never waits for it).
float OtherMoved(const Wheel &w, int player) {
    const float *now = player == 1 ? g_partner.pos : CharaMain.pos;
    float        dx = now[0] - w.other_at[0];
    float        dz = now[2] - w.other_at[2];

    return std::sqrt(dx * dx + dz * dz);
}

void LogClose(const Wheel &w, int player, const char *how) {
    std::fprintf(stderr, "coop-wheel: player %d's wheel closes (%s) after %d ticks, task %d; the other player moved %.1f meanwhile\n", player, how,
                 w.ticks, (int) gameTask, OtherMoved(w, player));
}

// coop-p2controls: a member's equipped weapon, else null (a stand-in's slot -1).
// coop-townrings: the party's status: the floor's on a floor, the save's in town (UserStatus is the floor's alone).
CUserStatus *PartyStatus() {
    return UserStatus != nullptr ? UserStatus : SaveData != nullptr ? (CUserStatus *) SaveData->GetDngStatus() : nullptr;
}

WEAPON_HAVE *EquippedOf(int chara) {
    CUserStatus *status = PartyStatus();
    const int    slot = status != nullptr && chara >= 0 && chara < 6 ? status->equipped_weapon_slot[chara] : -1;

    return slot >= 0 && slot < 11 && status->chara_weapons[chara][slot].item_no > 0 ? &status->chara_weapons[chara][slot] : nullptr;
}

// coop-apartrings (Jep's rc4 playtest 11-18: no ring opened in a building he had gone into alone; "the rings for everyone, everywhere"):
// apart in a town (alone outdoors, or alone in a building), this copy is its own player's world alone, and its game player is that
// player: the rings there are that player's, on the copy's own pad (the link is down apart: the game's pad, Pad(0)), stepped and drawn
// in slot 1 (his machinery: PlayerWheelMask holds the game port's buttons), read with that player's own options, their member, and
// picks as their Allies page takes them (TownAlliesApply: their own member, or another player's asked across zones). On a floor apart
// the game's own wheel and picker serve (OwnWheels). 0 when not apart in a town. DC_COOP_MUTATE=apartrings (twin): no rings apart.
// coop-seed324 (open item 2 of the rc5 soak seed 20261123324): while the link is up (a merge's lockstep ticks, the worlds still apart
// till the merge) a ring slot is its player on every copy: slot 1 is player 1's (his agreed pad steps it, NetplayPlayerPad(1)),
// read with player 1's row of the options table (CoopQuickSelectOnFor) and picked as player 1's Allies page takes it. Cause: each
// copy gave slot 1 its own player there (player 1 on the stayer, player 2 on the joiner), so the D-pad's quick-select reads, the
// window rule (InputInWindow) and a pick's TownAlliesApply differed between copies: OUT OF SYNC. Only with the link down is a
// copy its own player's world alone. DC_COOP_MUTATE=slotown (twin): the copy's own player (and its own options) while linked too.
int TownApartSelf() {
    if (!Enabled() || mode != GAME_MODE_EDIT || !CoopWorldsApart() || Mutated("apartrings")) {
        return 0;
    }
    return NetplayLinkUp() && !Mutated("slotown") ? 1 : CoopWorldsSelf();
}

// coop-townrings (Jep 11-17: "both rings selectable in town too"): linked and together in a town's walk outdoors, where her town
// walker stands beside the game's player: each player's rings there (his character ring, both element rings; hers was there).
bool TownRingPlace() {
    // coop-dpadmenus (Jep 11-18: the rings "for everyone, everywhere"): indoors too, both players in one building together (her walker on
    // this side of its door: TownPartnerShown). DC_COOP_MUTATE=ringsoutdoors (twin): outdoors only, as before.
    // (a building's walk is the room's own mode, ED_IN_MODE_WALK: the town's GameMode stays 12 there, its map jump)
    // coop-apartrings: apart, this copy's own player's (TownApartSelf), alone in the town or a building.
    return Enabled() && mode == GAME_MODE_EDIT && (EdInteriorFlag == 0 ? ::GameMode == ED_MODE_WALK : CoopTownIndoorWalks() && !Mutated("ringsoutdoors")) &&
           (TownApartSelf() > 0 || (NetplayLocalPlayer() != 0 && !CoopWorldsApart() && TownPartnerShown(nullptr, nullptr)));
}

// coop-apartrings: apart, the copy's game walker (its own player) is in something of its own: in a building its walk is the room's own
// mode (TownRingPlace: a talk or menu there leaves it); outdoors the town's modes (CoopTownPlayerBusy(0): a talk, menu, pause, Georama).
bool ApartWalkerBusy() {
    return EdInteriorFlag == 0 && CoopTownPlayerBusy(0) != nullptr;
}

// coop-apartrings: the player a ring slot belongs to: apart in a town, slot 1 is this copy's own player.
int SlotPlayer(int slot) {
    const int self = slot == 1 ? TownApartSelf() : 0;

    return self > 0 ? self : slot;
}

// Her town walker's member (the town's own), -1 none.
int HerTownMember() {
    int member = -1;

    return TownApartSelf() == 0 && TownPartnerShown(nullptr, &member) ? member : -1;
}

// His member in town: the members' word for player 1 (a trade's, his ring's), Toan when none. coop-apartrings: apart, this copy's
// own player's (the Allies page's word, TownMemberOf).
int HisTownMember() {
    if (const int self = TownApartSelf(); self > 0) {
        return TownMemberOf(self - 1);
    }
    const int member = Member(0);

    return member >= 0 && member < 6 ? member : CHARA_TOAN;
}

// coop-p2controls (Jep 11-17: her D-pad Up cycled her weapons, a holdover from pad 2's first combat): retail's element quick select,
// player 1's D-pad Up (DunMoveChara: game.element_quick_select on, a weapon with two choices or more, nothing under way), opens
// her own ring of her weapon's elements with retail's picker's rules (ElementChoicesOf, ElementChoiceOfCurrent) and icons. Retail's
// picker itself is a game task that stops the world for both and draws in player 1's menu texture block (coop-wheelcrash's
// reasons for her own party ring), so hers is this ring: on her screen, the world going on. Read with her own setting
// (CoopQuickSelectOnFor), whoever holds the game's pad.
void OpenElements(Wheel &w, int player, int chara) {
    WEAPON_HAVE *weapon = EquippedOf(chara);
    s8           choices[6] = {};
    const int    count = weapon != nullptr ? ElementChoicesOf(weapon, weapon, choices) : 0;

    if (count < 2) {
        // (coop-dpadmenus: and where the weapon was read: member, the status (floor's or save's), its stones)
        std::fprintf(stderr, "coop-wheel: player %d's D-pad Up opens no element ring (%d choices for item %d) at link tick %lld (member %d, %s status, stones %d,%d,%d,%d,%d)\n",
                     player, count, weapon != nullptr ? (int) weapon->item_no : -1, (long long) NetplayLinkTick(), chara, UserStatus != nullptr ? "floor" : "save",
                     weapon != nullptr ? (int) weapon->elem[0] : -1, weapon != nullptr ? (int) weapon->elem[1] : -1, weapon != nullptr ? (int) weapon->elem[2] : -1,
                     weapon != nullptr ? (int) weapon->elem[3] : -1, weapon != nullptr ? (int) weapon->elem[4] : -1);
        return;
    }

    w.elements = true;
    w.town = false;
    w.chara = chara;
    w.count = count;
    for (int i = 0; i < count; i++) {
        w.members[i] = choices[i];
    }
    w.selected = ElementChoiceOfCurrent(choices, count, weapon->best_elem);
    w.turn = 0;
    w.ticks = 0;
    w.opens++;
    std::copy(player == 1 ? g_partner.pos : CharaMain.pos, (player == 1 ? g_partner.pos : CharaMain.pos) + 3, w.other_at);
    w.open = true;
    LoadFaces();
    SndSePlay(MENU_SOUND_CURSOR, -1, 0);
    std::fprintf(stderr, "coop-wheel: player %d's element ring opens (%d elements, item %d's element %d first) at link tick %lld, task %d\n", player,
                 count, (int) weapon->item_no, (int) weapon->best_elem, (long long) NetplayLinkTick(), (int) gameTask);

    // coop-ringlog (the 11-18 playtest: an empty "no element" place on player 1's ring, not reproduced): always on, one line per open: each place's
    // element and cell with the texels that draw in it and their mean grey (the none cell empty = unpainted), the sheet's texture key
    // and format, and what last made or moved the sheet.
    {
        const CTexture *sheet = TexManager.GetTexture((char *) "wepicon", WheelBlock());
        char            places[256] = {};
        size_t          at = 0;

        for (int i = 0; i < count; i++) {
            at += std::snprintf(places + at, sizeof places - at, " %d:e%d%s", i, w.members[i], w.members[i] == WEAPON_ELEMENT_NONE ? "(none)" : "");
            at = std::min(at, sizeof places - 1);
        }
        if (sheet == nullptr) {
            std::fprintf(stderr, "coop-wheel: player %d's element ring: no wepicon sheet in block %d; sheet last %s at link tick %lld (%d times)\n", player,
                         WheelBlock(), g_sheet.what, g_sheet.tick, g_sheet.times);
        } else {
            const sceGsTex0 &tex0 = *reinterpret_cast<const sceGsTex0 *>(&sheet->tex0);

            std::fprintf(stderr, "coop-wheel: player %d's element ring icons (places in order):%s; none cell as the bank held it after the painter: %d texels drawn, mean grey %d; sheet key %u psm %u bytes/texel %d%s "
                         "last %s at link tick %lld (%d times)\n", player, places, g_sheet.bank_n, g_sheet.bank_m, (unsigned) tex0.TBP0, (unsigned) tex0.PSM, (int) sheet->bpp,
                         sheet->swizzled != 0 ? " swizzled" : "", g_sheet.what, g_sheet.tick, g_sheet.times);
        }
    }
}

// The ring's Cross: the weapon takes the element (PickerKey's Cross on the live weapon's WEAPON_HAVE; her weapon's element is read
// live by her hits, damage and ring shots).
void PickElement(const Wheel &w, int player) {
    WEAPON_HAVE *weapon = EquippedOf(w.chara);
    const int    element = w.members[w.selected];

    if (weapon == nullptr || weapon->best_elem == element) {
        std::fprintf(stderr, "coop-wheel: player %d's element ring keeps element %d at link tick %lld\n", player, element, (long long) NetplayLinkTick());
        return;
    }

    const int was = weapon->best_elem;

    weapon->best_elem = (s8) element;
    SndSePlay(MENU_SOUND_CONFIRM, -1, 0);
    std::fprintf(stderr, "coop-wheel: player %d's weapon (item %d) takes element %d (was %d) at link tick %lld\n", player, (int) weapon->item_no, element,
                 was, (long long) NetplayLinkTick());
}

} // namespace

bool WheelOpen() {
    // A swap question to player 2 takes the same buttons (its Cross and Circle answer it).
    return g_wheel.open || PromptAsksPartner();
}

int WheelJobOpens() {
    return g_wheels[0].opens + g_wheels[1].opens;
}

long long WheelJobStepped() {
    return (long long) std::max(g_wheels[0].stepped, g_wheels[1].stepped);
}

// Session reset: the wheel closed and its button edges forgotten.
void WheelResetForSession() {
    g_wheels[0] = Wheel{};
    g_wheels[1] = Wheel{};
    g_town_let_go = -1;
}

void CloseWheel(const char *how) {
    if (g_wheel.open && !g_wheel.town) {
        LogClose(g_wheel, 2, how);
    }
    g_wheel.open = false;
    g_wheel.turn = 0;
}

bool OwnWheels() {
    return Enabled() && NetplayLocalPlayer() != 0 && g_partner.loaded_chara >= 0 && !CoopWorldsApart() && !Mutated("gamewheel");
}

bool PlayerWheelOpen() {
    return g_wheels[0].open && g_wheels[0].stepped >= NetplayLinkTick() - 1;
}

std::uint16_t PlayerWheelMask() {
    const Wheel &w = g_wheels[0];

    return w.stepped >= NetplayLinkTick() - 1 ? w.mask : 0;
}

void CloseWheelForPause(int player) {
    if (Mutated("pausewheel")) {
        return;
    }
    if (player == 1 && g_wheels[0].open) {
        Wheel &w = g_wheels[0];

        LogClose(w, 1, "paused");
        w.open = false;
        w.turn = 0;
        w.mask = w.held; // what is still held stays the ring's until let go
    } else if (player == 2 && g_wheel.open && !g_wheel.town) {
        CloseWheel("paused");
    }
}

// Player 1's ring (coop-wheelcrash, Jep's 2026-10-27 playtest: his Select opened retail's wheel, which stops the whole
// game and draws on both screens, and beside her menu it loaded into her menu's texture block and buffer and both copies
// crashed). Linked and together on a floor, his Select opens this ring instead: the same ring as hers, round Toan, drawn
// on his copy alone, while the world and she go on. His buttons go to the ring (PlayerWheelMask), his stick still walks.
// A pick changes his member at once (PromptChangePlayer); the partner's member asks her (AskSwap).
bool StepPlayerWheel(const InputPadState &pad) {
    Wheel        &w = g_wheels[0];
    SoundOwner    sound(1); // coop-menuse
    std::uint16_t pressed = pad.buttons & ~w.held;
    const bool    stale = w.stepped < NetplayLinkTick() - 1;

    w.held = pad.buttons;
    w.stepped = NetplayLinkTick();
    w.mask &= pad.buttons;

    if (w.open && w.elements) {
        return true; // coop-p2controls3: his element ring steps from the menu tick (StepElementRings)
    }

    if (!w.open) {
        if (((pressed & kInputSelect) != 0 || DownOpens(pressed, pad, 1)) && OwnWheels() && CoopPlainPlay() && UserStatus->party_size >= 2 &&
            UserStatus->hp[UserStatus->cur_chara] > 0 && BtActStatus.action_on == 0 && !InputInWindow(1)) { // (coop-dpadmenus: one rule)
            OpenPlayer();
        } else if ((pressed & kInputSelect) != 0) {
            std::fprintf(stderr, "coop-wheel: player 1's Select opens no ring (own wheels %d, task %d, his menu %d, party %d, life %d, action %d, paused %d) at link tick %lld\n",
                         (int) OwnWheels(), (int) gameTask, (int) FullMenuOpenFor(1), (int) UserStatus->party_size,
                         (int) UserStatus->hp[UserStatus->cur_chara], (int) BtActStatus.action_on, (int) OwnPause(1), (long long) NetplayLinkTick());
        }
        if (w.open) {
            w.mask = 0xFFFF;
        }
        return w.open;
    }

    auto close = [&](const char *how) {
        LogClose(w, 1, how);
        w.open = false;
        w.turn = 0;
        w.mask = pad.buttons; // what is still held stays the ring's until let go
    };

    w.ticks++;

    // Gone from play (a floor change, an event, a menu of his, down), or his member changed under it: it closes.
    if (stale || !OwnWheels() || !CoopPlainPlay() || FullMenuOpenFor(1) || UserStatus->hp[UserStatus->cur_chara] <= 0 ||
        w.members[0] != UserStatus->cur_chara) {
        close("dropped");
        return false;
    }

    {
        int members[6];
        int count = PlayerMembers(members);

        Refresh(w, members, count, 1);
    }

    if (w.turn > 0) {
        w.turn--;
    }

    if ((pressed & (kTurnNext | kTurnBack)) != 0) {
        int step = (pressed & kTurnNext) != 0 ? 1 : -1;

        w.selected = (w.selected + step + w.count) % w.count;
        w.turn = TurnTicks();
        w.turn_direction = step;
        SndSePlay(MENU_SOUND_CURSOR, -1, 0);
    }

    if ((pressed & kInputCross) != 0) {
        int chosen = w.members[w.selected];

        if (!WheelTakes(0, chosen)) {
            SndSePlay(MENU_SOUND_REFUSE, -1, 0);
            std::fprintf(stderr, "coop-wheel: player 1's ring refuses %d at link tick %lld\n", chosen, (long long) NetplayLinkTick());
            w.mask = 0xFFFF;
            return true;
        }
        if (chosen != w.members[0]) {
            w.picks++;
        }
        close("picked");
        WheelConfirm(0, chosen);
        return true;
    }

    if ((pressed & (kInputCircle | kInputSelect)) != 0) {
        SndSePlay(MENU_SOUND_REFUSE, -1, 0);
        close("Circle");
        return true;
    }

    w.mask = 0xFFFF;
    return true;
}

// Test hook (coop-p2controls, DC_COOP_TEST_WEAPON_ELEMS=P:fire,ice,thunder,wind,holy): at press clock 1 player P's equipped weapon
// gets those element stones, on every copy alike, so the element ring has a choice to offer.
namespace {
int RingMember(int player); // (below: the member a player plays, on a floor or in town)
} // namespace

void TestWeaponElementsOf(const char *spec);

// coop-swapmenulook: "P:...;P:..." gives both players' weapons stones.
void TestWeaponElements() {
    static const char *spec = TestEnv("DC_COOP_TEST_WEAPON_ELEMS");

    // (coop-dpadmenus: in a building the press clock never runs, its walk being the room's own mode: there at the first tick the
    // rings may open after the test's state file is back (DC_STATE_LOAD_AT=T:PATH, put back at link tick T: before it the load undoes the
    // stones), on every copy alike)
    static bool                 indoors_done = false;
    static const char          *load_at = TestEnv("DC_STATE_LOAD_AT");
    static const long long      loaded_after = load_at != nullptr ? std::atoll(load_at) : -1;
    const bool                  indoors = !indoors_done && EdInteriorFlag != 0 && TownRingPlace() && NetplayLinkTick() > loaded_after + 1;

    if (spec == nullptr || (NetplayTestPressClock() != 1 && !indoors)) {
        return;
    }
    indoors_done = indoors_done || indoors;
    for (const char *at = spec; at != nullptr && *at != '\0'; at = std::strchr(at, ';') != nullptr ? std::strchr(at, ';') + 1 : nullptr) {
        TestWeaponElementsOf(at);
    }
}

void TestWeaponElementsOf(const char *spec) {
    int player = 0;
    int e[5] = {};
    int current = -1; // coop-p2controls3: "P:f,i,t,w,h/E" also gives the weapon element E (both rings then open on the same one)
    const int read = std::sscanf(spec, "%d:%d,%d,%d,%d,%d/%d", &player, &e[0], &e[1], &e[2], &e[3], &e[4], &current);

    if (read < 6) {
        return;
    }
    WEAPON_HAVE *weapon = EquippedOf(RingMember(player)); // (coop-townrings: in town too)

    if (weapon == nullptr) {
        std::fprintf(stderr, "coop-wheel: test: player %d has no weapon for the element stones\n", player);
        return;
    }
    for (int i = 0; i < 5; i++) {
        weapon->elem[i] = (decltype(weapon->elem[i])) e[i];
    }
    if (read == 7 && current >= 0 && current <= 5) {
        weapon->best_elem = (decltype(weapon->best_elem)) current;
    }
    if (player == 1 && UserStatus != nullptr && mode == GAME_MODE_DUNGEON) {
        SetWeaponAttachStatus(NowWeaponHave); // his live weapon rebuilt from it, as the game does after a weapon change (his picker reads it)
    }
    std::fprintf(stderr, "coop-wheel: test: player %d's weapon (item %d) has element stones %d,%d,%d,%d,%d, element %d (member %d, %s status)\n", player,
                 (int) weapon->item_no, e[0], e[1], e[2], e[3], e[4], (int) weapon->best_elem, RingMember(player), UserStatus != nullptr ? "floor" : "save");
}

bool StepWheel(const InputPadState &pad) {
    SoundOwner sound(2); // coop-menuse: pad 2's wheel

    std::uint16_t pressed = pad.buttons & ~g_wheel.held;
    // coop-wheelcrash: her Select opens it (Jep's 2026-10-27 playtest: "P2 pressing Select no longer opens her wheel").
    const bool    select = SelectOpens(g_wheel, pad.buttons, pressed, 2);

    g_wheel.held = pad.buttons;

    if (!g_wheel.open) {
        const bool free = !Mutated("wheel") && g_partner.down == 0 && !g_partner.attacking && g_partner.busy == 0 && g_partner.loaded_chara >= 0 &&
                          !InputInWindow(2); // (coop-dpadmenus: one rule)

        if ((pressed & kInputR3) != 0 && !R3Menu()) {
            std::printf("coop-wheel: pad 2 R3, no menu\n");
        } else if (((pressed & kInputR3) != 0 || select || DownOpens(pressed, pad, 2)) && free) { // (coop-dpadwheel: her D-pad Down)
            Open(g_partner.loaded_chara, false);
        }

        return g_wheel.open;
    }

    if (g_wheel.elements) {
        return true; // coop-p2controls3: her element ring steps from the menu tick (StepElementRings), the world held or not
    }

    g_wheel.ticks++;

    // Knocked down, or the member under it gone (the player took it): the wheel closes.
    if (g_partner.down > 0 || (g_wheel.elements ? g_wheel.chara : g_wheel.members[0]) != g_partner.loaded_chara) {
        CloseWheel();
        return false;
    }

    if (!g_wheel.town && !g_wheel.elements) { // coop-pt1111k: its entries follow the others' members
        int members[6];
        int count = PadMembers(g_partner.loaded_chara, false, members);

        Refresh(g_wheel, members, count, 2);
    }

    if (g_wheel.turn > 0) {
        g_wheel.turn--;
    }

    if ((pressed & (kTurnNext | kTurnBack)) != 0) {
        int step = (pressed & kTurnNext) != 0 ? 1 : -1;

        g_wheel.selected = (g_wheel.selected + step + g_wheel.count) % g_wheel.count;
        g_wheel.turn = TurnTicks();
        g_wheel.turn_direction = step;
        SndSePlay(MENU_SOUND_CURSOR, -1, 0);
    }

    if ((pressed & kInputCross) != 0) {
        int chosen = g_wheel.members[g_wheel.selected];

        if (!g_wheel.elements && !WheelTakes(1, chosen)) {
            SndSePlay(MENU_SOUND_REFUSE, -1, 0);
            std::fprintf(stderr, "coop-wheel: player 2's ring refuses %d at link tick %lld\n", chosen, (long long) NetplayLinkTick());
            return true;
        }
        if (g_wheel.elements || chosen != g_wheel.members[0]) {
            g_wheel.picks++;
        }
        if (g_wheel.elements) {
            PickElement(g_wheel, 2);
        } else {
            WheelConfirm(1, chosen);
        }
        CloseWheel("picked");
        return true;
    }

    if ((pressed & (kInputCircle | kInputR3 | kInputSelect)) != 0) {
        SndSePlay(MENU_SOUND_REFUSE, -1, 0);
        CloseWheel("Circle");
        return true;
    }

    return true;
}

// coop-p2controls3 (Jep 11-17: "the element switcher must be a local menu for each player that counts as a pause state", his too):
// each player's element quick select (D-pad Up, their own game.element_quick_select: DunMoveChara's rule) is their ring's element
// mode (OpenElements / PickElement, drawn as the game's element picker by DrawRing), a menu of their own: on their own screen, the
// world going on unless every player is in a pause state (coop_pause's PauseZoneHeld counts it). It steps from the menu tick on the
// players' own agreed pads, beside the pauses, every copy alike: the walk that steps the rings stops while the world holds, which
// would leave every player's ring and the world held for good. His was the game's own picker, which stops the world for every player.
namespace {

std::uint16_t g_ring_held[2]; // the agreed buttons each player's element ring last saw (presses are what is new)

int RingMember(int player) {
    if (TownRingPlace()) {
        return player == 1 ? HisTownMember() : HerTownMember(); // coop-townrings
    }
    return player == 1 ? (UserStatus != nullptr ? (int) UserStatus->cur_chara : -1) : g_partner.loaded_chara;
}

// coop-townrings: whether the player may open a ring in town now: none of their menus, pause, Georama or ring open.
bool TownRingFree(int player) {
    if (TownApartSelf() > 0) {
        // coop-apartrings: the copy's own player alone, the game's walker (CoopTownPlayerBusy(0): its talk, menu, pause or Georama)
        return player == 1 && !g_wheels[0].open && !ApartWalkerBusy();
    }
    if (player == 1) {
        return !CoopTownP1MenuLocal() && !PlayerInMenu(1, kActorMenuTown) && !g_wheels[0].open;
    }
    return !PlayerInMenu(2, kActorMenuTown) && !CoopP2TownPaused() && !CoopP2Building() && !g_wheels[1].open;
}

// Whether the player may open it this tick: on the floor under control, nothing of theirs under way (DunMoveChara's action_on and
// life for him, her walk's tests for her), no menu, pause, prompt, ring or presentation of theirs.
bool RingFree(int player) {
    if (player == 1 && CoopTestGamePicker()) {
        return false; // (the test's reference: his D-pad Up opens the game's own picker, CoopTestGamePicker)
    }
    if (TownRingPlace()) {
        return !Mutated("wheel") && !InputInWindow(SlotPlayer(player)) && TownRingFree(player); // coop-townrings (coop-dpadmenus: one rule)
    }
    if (Mutated("wheel") || !CoopPlainPlay() || BtEventMode != 0 || UserStatus == nullptr || !OwnWheels() || InputInWindow(player) ||
        UsePromptOpen(player) || PlayerPresenting(player) || g_wheels[player - 1].open) { // (coop-dpadmenus: one rule)
        return false;
    }
    if (player == 1) {
        return UserStatus->hp[UserStatus->cur_chara] > 0 && BtActStatus.action_on == 0;
    }
    return g_partner.visible && g_partner.down == 0 && !g_partner.attacking && g_partner.busy == 0 && g_partner.loaded_chara >= 0 &&
           !PartnerFirstPerson();
}

void CloseElementRing(int player, const char *how, std::uint16_t held) {
    Wheel &w = g_wheels[player - 1];

    if (player == 2) {
        CloseWheel(how);
        return;
    }
    LogClose(w, SlotPlayer(1), how);
    w.open = false;
    w.turn = 0;
    w.mask = held; // what is still held stays the ring's until let go
}

void StepElementRing(int player, const InputPadState &pad) {
    Wheel        &w = g_wheels[player - 1];
    SoundOwner    sound(player); // coop-menuse: its sounds are its player's
    std::uint16_t pressed = pad.buttons & ~g_ring_held[player - 1];

    g_ring_held[player - 1] = pad.buttons;
    if (!w.open) {
        if ((pressed & kInputUp) == 0 || (pad.buttons & kInputSelect) != 0 || !RingFree(player)) {
            return;
        }
        const bool on = CoopQuickSelectOnFor(SlotPlayer(player)); // (coop-apartrings: apart, the copy's own player's)

        std::fprintf(stderr, "coop-wheel: player %d's D-pad Up read with their element quick select %s at link tick %lld\n", SlotPlayer(player), on ? "on" : "off",
                     (long long) NetplayLinkTick());
        if (on) {
            OpenElements(w, SlotPlayer(player), RingMember(player));
            w.town = w.open && TownRingPlace(); // coop-townrings: (her town walker's ring is drawn and held as her town ring is)
        }
        if (w.open && player == 1) {
            w.stepped = NetplayLinkTick();
            w.mask = 0xFFFF;
        }
        return;
    }
    if (!w.elements) {
        return; // (the character ring: the walk's step)
    }

    w.ticks++;
    if (player == 1) {
        w.stepped = NetplayLinkTick(); // (his ring is drawn while stepped: PlayerWheelOpen)
        w.mask = 0xFFFF;
    }

    // Gone from play (a floor change, an event, a menu of theirs), down, or their member changed under it: it closes.
    const bool down = player == 1 ? UserStatus == nullptr || UserStatus->hp[UserStatus->cur_chara] <= 0 : g_partner.down > 0;

    // coop-townrings: in town, gone from the town's walk or into a menu of theirs
    const bool gone = w.town ? !TownRingPlace() || (TownApartSelf() > 0 ? ApartWalkerBusy() // (coop-apartrings)
                                                    : player == 1 ? CoopTownP1MenuLocal() || PlayerInMenu(1, kActorMenuTown)
                                                                 : PlayerInMenu(2, kActorMenuTown) || CoopP2TownPaused() || CoopP2Building())
                             : !CoopPlainPlay() || BtEventMode != 0 || down || FullMenuOpenFor(player) || !OwnWheels();

    if (gone || RingMember(player) != w.chara) {
        CloseElementRing(player, "dropped", pad.buttons);
        return;
    }

    if (w.turn > 0) {
        w.turn--;
    }

    if ((pressed & (kTurnNext | kTurnBack)) != 0) {
        int step = (pressed & kTurnNext) != 0 ? 1 : -1;

        w.selected = (w.selected + step + w.count) % w.count;
        w.turn = TurnTicks();
        w.turn_direction = step;
        SndSePlay(MENU_SOUND_CURSOR, -1, 0);
    }

    if ((pressed & kInputCross) != 0) {
        w.picks++;
        PickElement(w, SlotPlayer(player));
        CloseElementRing(player, "picked", pad.buttons);
        if (player == 1 && !w.town) {
            ElementAppliedToLiveWeapon(); // (his live weapon follows, as the game's picker's close does; hers is read as it draws; in town
                                          // no weapon is held)
        }
        return;
    }

    if ((pressed & (kInputCircle | kInputR3 | kInputSelect)) != 0) {
        SndSePlay(MENU_SOUND_REFUSE, -1, 0);
        CloseElementRing(player, "Circle", pad.buttons);
    }
}

} // namespace

// coop-townrings (Jep 11-17): player 1's character ring in town, on his D-pad Down (Character Quick Select; his Select there is the
// game's Georama: coop-georamaring), as on a floor:
// his own member first, then the party's others, never the member her town walker plays. A pick is his town member (SetMember,
// as a town trade sets his): his town body is drawn as it (coop-p1body); Toan stays the town's player. Stepped from the menu tick
// on his own agreed pad, every copy alike; his buttons the ring's while it is open (PlayerWheelMask), his stick walks on.
namespace {
std::uint16_t g_town_p1_held = 0;

bool TownP1Pickable(int chara) {
    CUserStatus *status = PartyStatus();

    return status != nullptr && chara >= 0 && chara < 6 && chara < status->party_size && chara != HerTownMember();
}

void OpenPlayerTown() {
    Wheel    &w = g_wheels[0];
    const int his = HisTownMember();

    w.count = 0;
    w.members[w.count++] = his;
    for (int k = 1; k < 6; k++) {
        const int chara = (his + k) % 6;

        if (TownP1Pickable(chara) || TownTrade(SlotPlayer(1) - 1, chara)) { // (coop-dpadmenus: another player's member too, a trade)
            w.members[w.count++] = chara;
        }
    }
    if (w.count < 2) {
        std::fprintf(stderr, "coop-wheel: player %d's town ring has nobody else to offer\n", SlotPlayer(1));
        return;
    }
    w.open = true;
    w.town = true;
    w.elements = false;
    w.selected = 0;
    w.turn = 0;
    w.ticks = 0;
    w.opens++;
    w.mask = 0xFFFF;
    LoadFaces();
    SndSePlay(MENU_SOUND_CURSOR, -1, 0);
    std::fprintf(stderr, "coop-wheel: player %d's wheel opens (%d members) at link tick %lld, in town%s\n", SlotPlayer(1), w.count,
                 (long long) NetplayLinkTick(), TownApartSelf() > 0 ? " (apart, this copy's own player)" : "");
}

void StepPlayerTownWheel(const InputPadState &pad) {
    Wheel        &w = g_wheels[0];
    SoundOwner    sound(1);
    std::uint16_t pressed = pad.buttons & ~g_town_p1_held;

    g_town_p1_held = pad.buttons;
    if (!w.open) {
        // coop-georamaring (the developer's rc5b playtest 11-19: "when p1 opens the georama menu, it also opens the character swap wheel and then
        // they both close instantly"): in a town Select is the game's (retail's Georama, EditLoop's EdPadDown(0x100)), so his ring opens
        // on its own button there, D-pad Down (Character Quick Select), as hers does (StepTownWheel: R3 or Down). Cause: his Select
        // opened his ring on the press that opened his Georama (the ring steps after the game read the press, the town's mode still
        // the walk); the next tick the ring dropped for the Georama with Select held, and the hold below ran out a tick later with
        // Select still down: the game read a second Select and closed the Georama. DC_COOP_MUTATE=georamaring (twin): his Select
        // opens it in town too, as before.
        const bool select = (pressed & kInputSelect) != 0 && Mutated("georamaring");

        if ((select || DownOpens(pressed, pad, SlotPlayer(1))) && TownRingPlace() && RingFree(1)) {
            OpenPlayerTown();
            w.stepped = NetplayLinkTick();
        } else if (w.mask != 0 && !Mutated("ringhold")) {
            // coop-georamaring: a closed ring's hold on what was still down as it closed (its Select, Circle or Cross) lasts till that
            // button is let go, as on a floor (StepPlayerWheel steps every tick): it is the ring's press, never the game's. Cause:
            // in town the ring was stepped only while open, so PlayerWheelMask ran out a tick after the close with the button
            // still down, and the game read it as a new press (a Select that shut the ring opened the Georama, a Cross talked).
            // DC_COOP_MUTATE=ringhold (twin): the hold runs out a tick after the close, as before.
            w.mask &= pad.buttons;
            w.stepped = NetplayLinkTick();
        }
        return;
    }
    if (!w.town || w.elements) {
        return; // (his floor ring, or his element ring: their own steps)
    }
    w.stepped = NetplayLinkTick();
    w.mask &= pad.buttons;
    w.ticks++;

    auto close = [&](const char *how) {
        LogClose(w, SlotPlayer(1), how);
        w.open = false;
        w.turn = 0;
        w.mask = pad.buttons; // what is still held stays the ring's until let go
    };

    if (!TownRingPlace() || (TownApartSelf() > 0 ? ApartWalkerBusy() : CoopTownP1MenuLocal() || PlayerInMenu(1, kActorMenuTown)) ||
        w.members[0] != HisTownMember()) {
        close("dropped");
        return;
    }
    if (w.turn > 0) {
        w.turn--;
    }
    if ((pressed & (kTurnNext | kTurnBack)) != 0) {
        int step = (pressed & kTurnNext) != 0 ? 1 : -1;

        w.selected = (w.selected + step + w.count) % w.count;
        w.turn = TurnTicks();
        w.turn_direction = step;
        SndSePlay(MENU_SOUND_CURSOR, -1, 0);
    }
    if ((pressed & kInputCross) != 0) {
        const int chosen = w.members[w.selected];

        if (chosen != w.members[0] && TownApartSelf() > 0) {
            w.picks++;
            TownAlliesApply(SlotPlayer(1), chosen); // coop-apartrings: apart, as the player's Allies page takes it (own, or a trade across zones)
            std::fprintf(stderr, "coop-wheel: player %d picks %d on their town wheel at link tick %lld (apart)\n", SlotPlayer(1), chosen,
                         (long long) NetplayLinkTick());
        } else if (chosen != w.members[0] && TownTrade(0, chosen)) {
            w.picks++;
            TownAlliesApply(1, chosen); // coop-dpadmenus: another player's member, asked for as a trade
            std::fprintf(stderr, "coop-wheel: player 1 picks %d on his town wheel at link tick %lld: player %d's, a trade\n", chosen,
                         (long long) NetplayLinkTick(), TownMemberHolder(chosen, 0) + 1);
        } else if (chosen != w.members[0] && TownP1Pickable(chosen)) {
            w.picks++;
            SetMember(0, chosen, MemberWhy::kPick);
            SndSePlay(MENU_SOUND_CONFIRM, -1, 0);
            std::fprintf(stderr, "coop-wheel: player 1 picks %d on his town wheel at link tick %lld\n", chosen, (long long) NetplayLinkTick());
        }
        close("picked");
        return;
    }
    if ((pressed & (kInputCircle | kInputSelect)) != 0) {
        SndSePlay(MENU_SOUND_REFUSE, -1, 0);
        close("Circle");
        return;
    }
    w.mask = 0xFFFF;
}
} // namespace

void StepElementRings(const InputPadState &one_in, const InputPadState &two_in) {
    // Each player's own agreed pad, as the pauses read theirs (the game's port has his ring's mask on it).
    InputPadState one = one_in, two = two_in;

    NetplayPlayerPad(1, one);
    NetplayPlayerPad(2, two);
    TestWeaponElements(); // coop-p2controls (coop-townrings: from here, in town too)
    StepElementRing(1, one);
    StepElementRing(2, two);
    if (mode == GAME_MODE_EDIT) {
        StepPlayerTownWheel(one); // coop-townrings: his character ring in town
    }

    // coop-georamaring: her rings' hold (RingHold), as his PlayerWheelMask: every button while one of her rings is open, and after it
    // closes what was still down till let go. Cause: her Georama (coop_georama_own.cpp) read her agreed pad with nothing over it, so
    // the Select that shut her element ring opened her Georama on the same press, and her Select under her open town ring opened her
    // Georama beneath it. Stepped here on every copy alike (agreed pads; g_wheels is in the state set). DC_COOP_MUTATE=herringhold
    // (twin): no hold, as before.
    Wheel &hers = g_wheels[1];

    hers.mask = Mutated("herringhold") ? 0 : hers.open ? 0xFFFF : (std::uint16_t) (hers.mask & two.buttons);
}

std::uint16_t RingHold(int player) {
    if (player == 1) {
        return PlayerWheelMask();
    }
    return player == 2 ? (std::uint16_t) (g_wheels[1].open && !Mutated("herringhold") ? 0xFFFF : g_wheels[1].mask) : 0;
}

bool ElementRingOpen(int player) {
    return player >= 1 && player <= 2 && g_wheels[player - 1].open && g_wheels[player - 1].elements;
}

// Pad 2's wheel in town (coop_town, plain walking only, so never in Georama's build mode where R3
// passes the cursor): the same ring, of the members pad 2 may play in town; a pick reloads the town
// partner on the next tick (TownMember follows g_partner.choice). DC_COOP_MUTATE=townwheel keeps it
// shut (twin).
bool TownWheelHoldsPad() {
    return (g_wheel.open && g_wheel.town) || g_town_let_go == NetplayLinkTick();
}

bool StepTownWheel(const InputPadState &pad, int loaded) {
    SoundOwner sound(2); // coop-menuse
    std::uint16_t pressed = pad.buttons & ~g_wheel.held;

    g_wheel.held = pad.buttons;

    if (!g_wheel.open) {
        if ((pressed & kInputR3) != 0 && !R3Menu()) {
            std::printf("coop-wheel: pad 2 R3, no menu\n");
        } else if (((pressed & kInputR3) != 0 || DownOpens(pressed, pad, 2)) && !Mutated("townwheel") && loaded > 0 &&
                   !InputInWindow(2)) { // (coop-dpadwheel; coop-dpadmenus: one rule)
            Open(loaded, true);
        }

        return g_wheel.open;
    }

    if (g_wheel.elements) {
        return true; // coop-townrings: her element ring in town steps from the menu tick (StepElementRings)
    }
    if (!g_wheel.town || g_wheel.members[0] != loaded) {
        CloseWheel();
        return false;
    }

    if (g_wheel.turn > 0) {
        g_wheel.turn--;
    }

    if ((pressed & (kTurnNext | kTurnBack)) != 0) {
        int step = (pressed & kTurnNext) != 0 ? 1 : -1;

        g_wheel.selected = (g_wheel.selected + step + g_wheel.count) % g_wheel.count;
        g_wheel.turn = TurnTicks();
        g_wheel.turn_direction = step;
        SndSePlay(MENU_SOUND_CURSOR, -1, 0);
    }

    if ((pressed & kInputCross) != 0) {
        int chosen = g_wheel.members[g_wheel.selected];

        if (chosen != loaded && TownTrade(1, chosen)) {
            g_wheel.picks++;
            TownAlliesApply(2, chosen); // coop-dpadmenus: another player's member, asked for as a trade
            std::fprintf(stderr, "coop-wheel: player 2 picks %d on her town wheel at link tick %lld: player %d's, a trade\n", chosen,
                         (long long) NetplayLinkTick(), TownMemberHolder(chosen, 1) + 1);
        } else if (chosen != loaded && TownMemberPickable(chosen)) {
            g_wheel.picks++; // coop-swapmenulook: retail's leaving
            if (SetPick(1, chosen, MemberWhy::kPick)) {
                SndSePlay(MENU_SOUND_CONFIRM, -1, 0);
                std::printf("coop: pad 2 picks %d on the wheel in town\n", chosen);
            } else {
                MemberRefused(1, chosen, MemberWhy::kPick);
            }
        }

        CloseWheel();
        g_town_let_go = NetplayLinkTick();
        return true;
    }

    if ((pressed & (kInputCircle | kInputR3)) != 0) {
        SndSePlay(MENU_SOUND_REFUSE, -1, 0);
        CloseWheel();
        g_town_let_go = NetplayLinkTick();
        return true;
    }

    return true;
}

namespace {

// coop-swapmenulook (Jep: "the game's own character swap menu and our co-op one look slightly different: different sized
// portraits in different places"; a duplicated menu must look like the original). The ring was drawn by hand here (faces 40 and
// 30 pixels, others dimmed, a 46-pixel ring with the chosen one on top above the player's head, a name line, no frame, no hand).
// It is now drawn by the game's own ring code (menu_dungeon.cpp's QuickRing pieces, split out of CharaChangeDraw and the element
// picker): 48-pixel faces (a fallen member dark, 0x40), retail's 60-pixel ring round the dungeon's QuickCharaPos with the chosen
// place on the left, its stay frame and hand, the screen behind at brightness 100 as the captured frame is drawn, the 1P/2P
// marks, the 20-frame turn and, after a pick, retail's leaving (the ring spreading, turning and fading). The world goes on.
// DC_COOP_MUTATE=ringlook (twin): the hand-made ring, as before.
constexpr int kRingBase[2] = {0x128, 0xD8}; // btitem.cpp: the dungeon's StartQuickChange positions
constexpr int kLeaveFrames[2] = {0x12, 0x12}; // [0] the party ring (CharaChangeKey's Loading), [1] the element picker (kCloseFrames + 1)

void DrawHandRing(const Wheel &w, const float pos[3]);

// One ring on this screen; leave >= 0: retail's leaving after a pick, that many frames in (no frame, no hand).
void DrawRing(const Wheel &w, const float pos[3], int leave) {
    if (Mutated("ringlook")) {
        if (leave < 0) {
            DrawHandRing(w, pos);
        }
        return;
    }

    CTexture *faces = TexManager.GetTexture((char *) "quickchara", WheelBlock());
    CTexture *icons = TexManager.GetTexture((char *) "wepicon", WheelBlock());
    CTexture *stay = TexManager.GetTexture((char *) "stayframe", -1);

    if (faces == nullptr || (w.elements && icons == nullptr)) {
        return;
    }

    // The screen behind: retail draws the frame it captured at brightness 100 (FrameImageDraw(100, 0x80)); here the world goes on
    // behind, dimmed alike (black at 0x80 - 100 over it: the GS's (0 - Cd) * 28 / 128 + Cd).
    AllFadeForMenu(0x80 - 100);
    setbilinear(0);
    TexManager.ReloadTexture(Vif1Packet, WheelBlock());

    float      radius = 60.0f;
    const int  alpha = leave >= 0 ? std::max(0x80 - leave * 4, 0) : 0x80;
    float      frames = (float) w.turn;
    float      direction = (float) w.turn_direction;

    if (leave >= 0) {
        // retail's leaving: changeMenu_long grows by the frame count each frame, the ring turns on (direction 0: forward)
        for (int k = 1; k <= leave; k++) {
            radius += (float) k;
        }
        frames = (float) leave;
        direction = 0.0f;
    }

    for (int i = 0; i < w.count; i++) {
        const int slot = (i - w.selected + w.count) % w.count;
        float     x = 0.0f;
        float     y = 0.0f;

        QuickRingPoint(slot, w.count, radius, frames, direction, &x, &y);
        x += (float) kRingBase[0];
        y += (float) kRingBase[1];
        if (w.elements) {
            QuickRingElement(icons, w.members[i], x, y, alpha);
            if (static const bool trace = TestEnvOn("DC_COOP_WHEEL_TRACE"); trace) {
                int u = 0, v = 0, cell = 0;

                ElementIconCell(w.members[i], &u, &v, &cell);
                std::fprintf(stderr, "coop-wheel: draw: element ring icon %d element %d%s at %d,%d size %d cell %d,%d\n", i, w.members[i],
                             i == w.selected ? " (selected)" : "", (int) (x + 8.0f), (int) (y + 8.0f), cell, u, v);
            }
        } else {
            const int chara = w.members[i];

            // coop-dpadwheel: in town the floor's status (UserStatus) is not there: the party's is the save's (her town ring, now on her
            // D-pad Down, read it and her copy crashed).
            const CUserStatus *status = UserStatus != nullptr ? UserStatus : SaveData != nullptr ? (const CUserStatus *) SaveData->GetDngStatus() : nullptr;

            QuickRingFace(faces, chara, x, y, status != nullptr && status->hp[chara] <= 0 ? 0x40 : 0x80, alpha);
            CoopCharaWheelMark(chara, x, y, alpha);
        }
    }

    if (leave < 0) {
        int hand_x = 0;
        int hand_y = 0;

        QuickRingFrame(kRingBase[0], kRingBase[1], radius, stay);
        QuickRingHandStep();
        QuickRingHandAt(kRingBase[0], kRingBase[1], radius, &hand_x, &hand_y);
        QuickRingHand(hand_x, hand_y);
    }
    setbilinear(1);
}

// The hand-made ring (DC_COOP_MUTATE=ringlook), as it was: above the head at pos.
void DrawHandRing(const Wheel &w, const float pos[3]) {
    static const char *const kNames[6] = {"Toan", "Xiao", "Goro", "Ruby", "Ungaga", "Osmond"};
    CTexture                *faces = TexManager.GetTexture((char *) "quickchara", WheelBlock());

    if (faces == nullptr) {
        return;
    }

    // Above the head on screen, else the middle of the screen.
    float centre_x = 320.0f;
    float centre_y = 200.0f;
    float head[4] = {pos[0], pos[1] + 12.0f, pos[2], 1.0f};
    int   screen[4];

    if (MGRotTransPers(screen, head, 0) != 0) {
        centre_x = (float) ((screen[0] - 0x6C00) >> 4);
        // The GS works in fields: its y is half the 2D drawing's. The ring floats above the head.
        centre_y = (float) (((screen[1] - GS_Y_OFFSET) >> 4) * 2) - 64.0f;
    }

    constexpr float kRadius = 46.0f;
    float           step = TWO_PI / (float) w.count;
    float           turn = step * (float) w.turn / (float) TurnTicks() * (float) w.turn_direction;

    TexManager.ReloadTexture(Vif1Packet, WheelBlock());

    // coop-p2controls: the element ring: retail's picker's icons (the bank's wepicon sheet), the rest as the party ring. With
    // DC_COOP_WHEEL_TRACE=1 each icon drawn is logged (a draw-mode test reads them).
    if (w.elements) {
        CTexture  *icons = TexManager.GetTexture((char *) "wepicon", WheelBlock());
        static const bool trace = TestEnvOn("DC_COOP_WHEEL_TRACE");

        for (int i = 0; i < w.count && icons != nullptr; i++) {
            const bool top = i == w.selected;
            float      angle = -HALF_PI + step * (float) (i - w.selected) + turn;
            int        size = top ? 40 : 30;
            int        x = (int) (centre_x + kRadius * cosf(angle)) - size / 2;
            int        y = (int) (centre_y + kRadius * sinf(angle)) - size / 2;
            int        u = 0, v = 0, cell = 0;
            u_char     shade = top ? 0x80 : 0x50;

            ElementIconCell(w.members[i], &u, &v, &cell);
            set2DSprite(Vif1Packet, icons, CRect_i_(x, y, size, size), CRect_i_(u, v, cell, cell), shade, shade, shade, 0x80);
            if (trace) {
                std::fprintf(stderr, "coop-wheel: draw: element ring icon %d element %d%s at %d,%d size %d cell %d,%d\n", i, w.members[i], top ? " (selected)" : "",
                             x, y, size, u, v);
            }
        }
        if (icons == nullptr && trace) {
            std::fprintf(stderr, "coop-wheel: draw: element ring has no wepicon texture\n");
        }
        return;
    }

    for (int i = 0; i < w.count; i++) {
        int    chara = w.members[i];
        bool   top = i == w.selected;
        float  angle = -HALF_PI + step * (float) (i - w.selected) + turn;
        int    size = top ? 40 : 30;
        int    x = (int) (centre_x + kRadius * cosf(angle)) - size / 2;
        int    y = (int) (centre_y + kRadius * sinf(angle)) - size / 2;
        int    u = (chara % 3) * 0x30;
        int    v = (chara / 3) * 0x30;
        u_char shade = top ? 0x80 : 0x50;

        set2DSprite(Vif1Packet, faces, CRect_i_(x, y, size, size), CRect_i_(u, v, 0x30, 0x30), shade, shade, shade, 0x80);
        if (static const bool trace = TestEnvOn("DC_QUICK_RING_TRACE"); trace) { // the same line as the game's ring's pieces
            std::fprintf(stderr, "quick ring: face %d at %d,%d size %d texel %d,%d shade %d alpha %d\n", chara, x, y, size, u, v, (int) shade, 0x80);
        }
    }

    dc::text::caption::LineStyle style;

    style.status_font = true;
    style.edge_width = 1.5f;

    int         chosen = w.members[w.selected];
    std::string label = chosen == w.members[0] ? std::string(kNames[chosen]) : std::string("> ") + kNames[chosen];

    dc::text::caption::DrawLine(label, centre_x - 30.0f, centre_y + kRadius + 18.0f, 9.0f, 1.0f, 120.0f, style, 1.0f, 1.0f, nullptr);
}

} // namespace

// coop-pt1111k: a ring's Cross on chosen, as it is taken (every copy on one tick): the other's member is asked for (a trade);
// another is the player's own pick, checked here against the floor's rule (ZonePickLegal), whatever the ring offered when
// it was drawn. A pick that would leave a character-limited floor's member unplayed is refused on the picker's screen.
void WheelConfirm(int index, int chosen) {
    SoundOwner sound(index + 1);

    if (index == 0) {
        if (chosen == g_partner.loaded_chara) {
            AskSwap(0, chosen);
        } else if (chosen != UserStatus->cur_chara && PlayerPickable(chosen)) {
            if (!ZonePickLegal(0, chosen)) {
                ZonePickRefused(0, chosen, "radial");
                return;
            }
            SndSePlay(MENU_SOUND_CONFIRM, -1, 0);
            std::fprintf(stderr, "coop-wheel: player 1 picks %d on his wheel\n", chosen);
            // coop-p2controls: retail's quick change around the change (which drops the lock-on, LockOffTargte): kept with his own
            // Swap Keeps Lock-On, as DunMoveChara's ring does. His swaps always dropped it.
            const bool keep = CoopSwapKeepsLockOnFor(1);
            const bool locked = lockOnTargetFlag != 0;

            QuickChangeKeepLockOn(keep);
            PromptChangePlayer(chosen);
            QuickChangeLockOnBack();
            std::fprintf(stderr, "coop: player 1's swap: his lock-on %s (his Swap Keeps Lock-On %s) at link tick %lld\n",
                         !locked ? "none" : lockOnTargetFlag != 0 ? "kept" : "dropped", keep ? "on" : "off", (long long) NetplayLinkTick());
        }
        return;
    }
    // The player's member is asked for, not taken (the same-character rule, coop_prompt).
    if (chosen == UserStatus->cur_chara && chosen != g_partner.loaded_chara) {
        AskSwap(1, chosen);
    } else if (chosen != g_partner.loaded_chara && Pickable(chosen)) {
        if (!ZonePickLegal(1, chosen)) {
            ZonePickRefused(1, chosen, "radial");
            return;
        }
        if (!SetPick(1, chosen, MemberWhy::kPick)) {
            MemberRefused(1, chosen, MemberWhy::kPick);
            return;
        }
        SndSePlay(MENU_SOUND_CONFIRM, -1, 0);
        std::printf("coop: pad 2 picks %d on the wheel\n", chosen);
        PartnerSwapLock(CoopSwapKeepsLockOnFor(2)); // coop-p2controls: retail's quick change lock-on, her own setting
    }
}

bool WheelOffersForTest(int index, int chara) {
    int members[6];
    int count = index == 0 ? PlayerMembers(members) : PadMembers(g_partner.loaded_chara, false, members);

    return std::find(members + 1, members + count, chara) != members + count;
}

bool WheelOpenForTest(int index, int chara) {
    Wheel &w = g_wheels[index == 0 ? 0 : 1];

    if (index == 0) {
        OpenPlayer();
    } else {
        Open(g_partner.loaded_chara, false);
    }
    for (int i = 1; i < w.count; i++) {
        if (w.members[i] == chara) {
            w.selected = i;
            return w.open;
        }
    }
    return false;
}

} // namespace coop

using namespace coop;

// coop-hudicon: one member's wheel face (quickchr.img's 48 by 48 picture), size by size at x, y.
bool CoopDrawHudFace(int chara, int x, int y, int size) {
    return CoopDrawHudFaceFaded(chara, x, y, size, 0x80);
}

bool CoopDrawHudFaceFaded(int chara, int x, int y, int size, int alpha) {
    if (chara < 0 || chara >= 6) {
        return false;
    }

    // Loaded again after anything emptied the block (a new floor), tried at most once a second when it will not load.
    static int wait = 0;
    CTexture  *faces = TexManager.GetTexture((char *) "quickchara", WheelBlock());

    if (faces == nullptr) {
        if (wait > 0) {
            wait--;
            return false;
        }

        LoadFaces();
        faces = TexManager.GetTexture((char *) "quickchara", WheelBlock());

        if (faces == nullptr) {
            wait = 60;
            return false;
        }
    }

    TexManager.ReloadTexture(Vif1Packet, WheelBlock());
    set2DSprite(Vif1Packet, faces, CRect_i_(x, y, size, size), CRect_i_((chara % 3) * 0x30, (chara / 3) * 0x30, 0x30, 0x30), 0x80, 0x80, 0x80,
                std::clamp(alpha, 0, 0x80));
    return true;
}

void CoopDrawWheel() {
    // coop-usebands2 (Jep's 2026-11-01 LAN pack 7 run: his Use Item window at the gate lock drawn in two, its left part moved
    // off by a sixth of the screen): the dungeon draws its HUD under the status panel's anchor (each piece whose centre lies
    // left of 0x124 goes to a wide window's left edge, the rest stay centred), and this is called inside it. These windows,
    // prompts and rings are not pieces of the HUD: each is placed in the frame whole. DC_COOP_MUTATE=keyanchor (twin): drawn
    // under the HUD's anchor, as before.
    std::optional<gfx::UiAnchorScope> whole;

    if (!Mutated("keyanchor")) {
        whole.emplace(gfx::UiAnchor{});
    }

    CoopDrawPrompt(); // the swap question, for whichever player it asks
    DrawKeyPick();    // coop-keypick: her key picker at a gate's lock, on her copy
    DrawUsePrompts(); // coop-p2prompt: her chest's or gate's prompt, on her copy

    // coop-wheelcrash: each player's ring on that player's own screen (couch: the one screen shows both).
    // DC_COOP_MUTATE=wheelboth draws each on both copies, as before (the twin).
    for (int player = 1; player <= 2; player++) {
        const Wheel &w = g_wheels[player - 1];
        // Round its player: player 1's round Toan; pad 2's round the partner it was opened for (the dungeon's, or the town's).
        float pos[3] = {g_partner.pos[0], g_partner.pos[1], g_partner.pos[2]};
        bool  shown = player == 1 ? PlayerWheelOpen() : w.town ? TownPartnerShown(pos, nullptr) : g_partner.visible;

        if (player == 1) {
            std::copy(CharaMain.pos, CharaMain.pos + 3, pos);
        }
        // coop-swapmenulook: retail's leaving after a pick (draw-only frames on this copy, kLeaveFrames of them)
        static int seen_picks[2] = {};
        static int leave[2] = {-1, -1};

        if (seen_picks[player - 1] != w.picks) {
            seen_picks[player - 1] = w.picks;
            leave[player - 1] = w.open ? -1 : 0;
        }
        if (w.open) {
            leave[player - 1] = -1;
        }

        // (coop-apartrings: apart in a town, slot 1 is this copy's own player's; coop-seed324: with the link up, player 1's: his copy draws it)
        const bool here = NetplayLocalPlayer() == 0 || NetplayLocalPlayer() == SlotPlayer(player) || (player == 1 && TownApartSelf() > 0 && !NetplayLinkUp()) ||
                          Mutated("wheelboth");

        if (!w.open && leave[player - 1] >= 0) {
            if (here && leave[player - 1] <= kLeaveFrames[w.elements ? 1 : 0]) {
                DrawRing(w, pos, leave[player - 1]);
                leave[player - 1]++;
            } else {
                leave[player - 1] = -1;
            }
            continue;
        }
        if (!w.open || !shown) {
            continue;
        }

        static int logged[2] = {};

        if (logged[player - 1] != w.opens) {
            logged[player - 1] = w.opens;
            std::fprintf(stderr, "coop-wheel: player %d's wheel %s on this copy (player %d)\n", SlotPlayer(player), here ? "drawn" : "kept off the screen",
                         NetplayLocalPlayer());
        }
        if (!here) {
            continue;
        }
        DrawRing(w, pos, -1);
    }
}

bool CoopOwnWheels() {
    return OwnWheels();
}

// Test hook (coop-p2controls3, DC_COOP_TEST_GAME_PICKER=1): linked, his D-pad Up opens the game's own element picker (which stops the
// world for both, as it did), not his ring, for a test that compares her ring with it on her screen (DCO-173's).
bool CoopTestGamePicker() {
    static const bool on = TestEnvOn("DC_COOP_TEST_GAME_PICKER");

    return on;
}

// (coop-wheeltwin49, train 49's t-wheel-crash~twin scored CRASH: its gamewheel mutation lets retail's wheel open beside her menu,
// which takes her menu's texture block, and her copy died in DrawPersonalBoard the same frame, faster than any stop line.
// DC_COOP_TEST_WHEEL_CLASH=1: when the wheel would open so, the copy says it and stops cleanly with the wheel still shut.)
// The menu beside the world is a local one since localmenu3d (train 48): its engine runs on its owner's copy alone, so
// FullMenuLive is false everywhere and the guard let retail's wheel open beside it; the in-menu bits are alike on every copy.
bool CoopGameWheelBlocked() {
    if (!Enabled() || !(FullMenuLive() || FullMenuOpenFor(1) || FullMenuOpenFor(2))) {
        return false;
    }
    if (!Mutated("gamewheel")) {
        return true;
    }
    if (static const bool clash = TestEnvOn("DC_COOP_TEST_WHEEL_CLASH"); clash) {
        std::fprintf(stderr, "coop-wheel: test: retail's wheel opens beside a menu next to the world (its texture block is the menu's) at link tick %lld: stopping before the draw\n",
                     (long long) NetplayLinkTick());
        dc::app::GameRequestStop();
        return true;
    }
    return false;
}

namespace coop {
namespace {
// coop-seed324: this copy's own rings across a state set's apply, for the twin alone.
struct RingsKept {
    bool          on = false;
    Wheel         wheels[2];
    std::uint16_t town_p1_held = 0;
    std::uint16_t ring_held[2] = {};
    long long     town_let_go = -1;
};

RingsKept g_rings_own;
} // namespace

// coop-seed324 (rc5 soak seed 20261123324): the rings' stepped state is lockstep state (player 1's ring holds the game port's
// buttons, PlayerWheelMask; each ring reads presses against the buttons it last saw), so it goes in the state set
// (build/snapshot-allow.txt) and a joiner steps the sender's. DC_COOP_MUTATE=ringset_own (twin): this copy's own kept, as before.
void WheelApplyBefore() {
    g_rings_own.on = Mutated("ringset_own");
    if (g_rings_own.on) {
        std::copy(std::begin(g_wheels), std::end(g_wheels), std::begin(g_rings_own.wheels));
        std::copy(std::begin(g_ring_held), std::end(g_ring_held), std::begin(g_rings_own.ring_held));
        g_rings_own.town_p1_held = g_town_p1_held;
        g_rings_own.town_let_go = g_town_let_go;
    }
}

void WheelApplyAfter() {
    if (g_rings_own.on) {
        std::copy(std::begin(g_rings_own.wheels), std::end(g_rings_own.wheels), std::begin(g_wheels));
        std::copy(std::begin(g_rings_own.ring_held), std::end(g_rings_own.ring_held), std::begin(g_ring_held));
        g_town_p1_held = g_rings_own.town_p1_held;
        g_town_let_go = g_rings_own.town_let_go;
        g_rings_own.on = false;
        std::fprintf(stderr, "coop-wheel: test: the state set's rings not taken (ringset_own): this copy keeps its own\n");
    }
    // (the tests read it: player 1's ring as the apply leaves it, the buttons it holds off the game's port)
    std::fprintf(stderr, "coop-wheel: after the state set's apply at link tick %lld: player 1's ring open %d, holds %04x off the game (stepped at %lld)\n",
                 (long long) NetplayLinkTick(), (int) g_wheels[0].open, (unsigned) g_wheels[0].mask, (long long) g_wheels[0].stepped);
}
} // namespace coop
