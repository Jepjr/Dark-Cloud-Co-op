#include "coop/game_ns.hpp"
#include "coop/coop.hpp"
#include "coop/coop_audio.hpp" // coop-menuse
#include "coop/coop_wheel.hpp"

#include <libvu0.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
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

namespace dc::game {
extern s32 gameTask;
} // namespace dc::game

#include "coop/coop_core.hpp"
#include "coop/coop_fullmenu.hpp" // coop-wheelcrash
#include "coop/coop_interact.hpp" // coop-keypick
#include "coop/coop_party.hpp"
#include "coop/coop_pause.hpp" // coop-pausewheel
#include "coop/coop_prompt.hpp"
#include "coop/coop_town.hpp"
#include "coop/coop_worlds.hpp" // coop-wheelcrash
#include "coop/coop_zonechara.hpp" // coop-pt1111k
#include "coop/netplay.hpp"

namespace coop {

namespace {

// Ticks a turn of the ring takes to show (drawing only reads it).
constexpr int kTurnTicks = 6;

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
    // Player 1's: the buttons the game must not see (all while open; after it closes, those still held).
    std::uint16_t mask = 0;
    std::int64_t  stepped = -10; // the link tick it last stepped on
    int           ticks = 0;
    float         other_at[3] = {}; // the other player's place as it opened (the world goes on meanwhile)
    int           opens = 0; // how many times it opened (the draw's log, which is this copy's own, keys on it)
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

void LoadFaces() {
    if (TexManager.GetTexture((char *) "quickchara", WheelBlock()) != nullptr) {
        return;
    }

    if (g_faces_file.empty()) {
        g_faces_file.resize(256 * 1024 + 64);
    }

    u_char *data = g_faces_file.data() + ((64 - reinterpret_cast<std::uintptr_t>(g_faces_file.data()) % 64) % 64);
    int     size = 0;

    if (LoadFile((char *) "commenu/quickchr.img", data, &size) == 0 || size <= 0 || size > 256 * 1024) {
        std::fprintf(stderr, "coop: cannot load commenu/quickchr.img\n");
        return;
    }

    LOADTEXTURE_INFO2 table[2] = {
        {(char *) data, WheelBlock(), 0},
        {nullptr,       0,                  0},
    };

    TexManager.DeleteTextureBlock(WheelBlock());
    TexManager.CleanUpTextureList();
    TexManager.LoadTextureBlockEX(WheelBlock(), table);
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
int PlayerMembers(int *out) {
    int loaded = UserStatus->cur_chara;
    int count = 0;

    out[count++] = loaded;
    for (int k = 1; k < 6; k++) {
        int chara = (loaded + k) % 6;

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

void OpenPlayer() {
    Wheel &w = g_wheels[0];

    w.town = false;
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
int PadMembers(int loaded, bool town, int *out) {
    int count = 0;

    out[count++] = loaded;
    for (int k = 1; k < 6; k++) {
        int chara = (loaded + k) % 6;

        if (town ? TownMemberPickable(chara) : Pickable(chara) || (chara == UserStatus->cur_chara && loaded < UserStatus->party_size && !Mutated("wheelask"))) {
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

// coop-pausewheel (Jep's pack 7 playtest: the ring opened while the game was paused): a player's own pause keeps his or
// her ring shut (and CloseWheelForPause closes it as the pause starts); the other player's pause leaves it alone.
// DC_COOP_MUTATE=pausewheel (twin): the pause is not looked at, as before.
bool OwnPause(int player) {
    return PausePlayer(player) && !Mutated("pausewheel");
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

    if (!w.open) {
        if ((pressed & kInputSelect) != 0 && OwnWheels() && gameTask == GAME_TASK_PLAY && !FullMenuOpenFor(1) && UserStatus->party_size >= 2 &&
            UserStatus->hp[UserStatus->cur_chara] > 0 && BtActStatus.action_on == 0 && !PromptAsksPartner() && !OwnPause(1)) {
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
    if (stale || !OwnWheels() || gameTask != GAME_TASK_PLAY || FullMenuOpenFor(1) || UserStatus->hp[UserStatus->cur_chara] <= 0 ||
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
        w.turn = kTurnTicks;
        w.turn_direction = step;
        SndSePlay(MENU_SOUND_CURSOR, -1, 0);
    }

    if ((pressed & kInputCross) != 0) {
        int chosen = w.members[w.selected];

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

bool StepWheel(const InputPadState &pad) {
    SoundOwner sound(2); // coop-menuse: pad 2's wheel
    std::uint16_t pressed = pad.buttons & ~g_wheel.held;
    // coop-wheelcrash: her Select opens it (Jep's 2026-10-27 playtest: "P2 pressing Select no longer opens her wheel").
    const bool    select = SelectOpens(g_wheel, pad.buttons, pressed, 2);

    g_wheel.held = pad.buttons;

    if (!g_wheel.open) {
        const bool free = !Mutated("wheel") && !PromptAsksPartner() && g_partner.down == 0 && !g_partner.attacking && g_partner.busy == 0 &&
                          g_partner.loaded_chara >= 0 && !FullMenuOpenFor(2) && !OwnPause(2);

        if ((pressed & kInputR3) != 0 && !R3Menu()) {
            std::printf("coop-wheel: pad 2 R3, no menu\n");
        } else if (((pressed & kInputR3) != 0 || select) && free) {
            Open(g_partner.loaded_chara, false);
        }

        return g_wheel.open;
    }

    g_wheel.ticks++;

    // Knocked down, or the member under it gone (the player took it): the wheel closes.
    if (g_partner.down > 0 || g_wheel.members[0] != g_partner.loaded_chara) {
        CloseWheel();
        return false;
    }

    if (!g_wheel.town) { // coop-pt1111k: its entries follow the others' members
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
        g_wheel.turn = kTurnTicks;
        g_wheel.turn_direction = step;
        SndSePlay(MENU_SOUND_CURSOR, -1, 0);
    }

    if ((pressed & kInputCross) != 0) {
        int chosen = g_wheel.members[g_wheel.selected];

        WheelConfirm(1, chosen);
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
        } else if ((pressed & kInputR3) != 0 && !Mutated("townwheel") && loaded > 0) {
            Open(loaded, true);
        }

        return g_wheel.open;
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
        g_wheel.turn = kTurnTicks;
        g_wheel.turn_direction = step;
        SndSePlay(MENU_SOUND_CURSOR, -1, 0);
    }

    if ((pressed & kInputCross) != 0) {
        int chosen = g_wheel.members[g_wheel.selected];

        if (chosen != loaded && TownMemberPickable(chosen)) {
            g_partner.choice = chosen;
            SndSePlay(MENU_SOUND_CONFIRM, -1, 0);
            std::printf("coop: pad 2 picks %d on the wheel in town\n", chosen);
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

// One ring, above the head at pos.
void DrawRing(const Wheel &w, const float pos[3]) {
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
    float           turn = step * (float) w.turn / (float) kTurnTicks * (float) w.turn_direction;

    TexManager.ReloadTexture(Vif1Packet, WheelBlock());

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
            PromptChangePlayer(chosen);
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
        g_partner.choice = chosen;
        SndSePlay(MENU_SOUND_CONFIRM, -1, 0);
        std::printf("coop: pad 2 picks %d on the wheel\n", chosen);
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
                0x80);
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
        if (!w.open || !shown) {
            continue;
        }

        const bool here = NetplayLocalPlayer() == 0 || NetplayLocalPlayer() == player || Mutated("wheelboth");

        static int logged[2] = {};

        if (logged[player - 1] != w.opens) {
            logged[player - 1] = w.opens;
            std::fprintf(stderr, "coop-wheel: player %d's wheel %s on this copy (player %d)\n", player, here ? "drawn" : "kept off the screen",
                         NetplayLocalPlayer());
        }
        if (!here) {
            continue;
        }
        DrawRing(w, pos);
    }
}

bool CoopOwnWheels() {
    return OwnWheels();
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
