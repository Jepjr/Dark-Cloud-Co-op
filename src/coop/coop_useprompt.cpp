// coop-p2prompt (Jep, 2026-10-27: no menu or prompt pauses the other player, and player 2's interactions must really work,
// not show player 1's): the prompts an object's event script would show, run for the player who used it beside the running
// game. Event scripts run as the game's one task (GAME_TASK_SCRIPT_*), so her Cross at a locked or trapped chest, at the
// gate's lock or at the floor's exit gate stopped both lockstep copies and blacked his screen ("Xiao is choosing...") while
// she read the script's messages, which showed and asked player 1 (dungeon suite F4).
//
// The scripts behind those objects were read from each dungeon's event.stb (the system script; programs 15, 16 and the
// gate's are alike in all seven dungeons):
//   15  a large chest with a trap (1 to 4): message 30, then 31's choice: leave / use a Treasure Key (none: 33; one: 35, the
//       trap off, the key spent) / open. Open: with Mardan Garayan caught, 39 and then 43 (trap 1: off) or the trap's own
//       choice (40 to 42); without, 32's choice (four rows). Row 0 leaves; trap 1 or the right guess (row + 1 == trap): 34,
//       the trap off; else trap 2: 36 and poison, 3: 37 and a bomb, 4: 38 and a curse, the trap still on. A chest whose trap
//       is off opens on the next Cross as any large chest.
//   16  the clown's chest (trap 5): a cutscene, message 50 and 51's choice of the floor's two clown items (GetPieroItem); the
//       pick is given and the lid opens (_SET_IBOX_FINISH).
//   gate (out_N): a cutscene, then the escape window: 1 the next floor (program 100), 2 back to town with the next floor
//       reached (_SET_FLOOR_LEVEL + _MAP_JUMP); her prompt offers both, and the answer goes to the window.
//   Any slot whose Cross (action mode 1) only shows one message and returns (the gate's lock: "It's locked", 200 in Wise Owl
//       Forest) is read from its bytecode (CrossMessage).
// Here those run as a prompt of the user's own: it steps on the user's agreed pad on both linked copies (so they agree, as the
// key picker does), is drawn on the user's copy only, and its result is applied on both on one tick, with the game's own
// texts (the floor's script messages). The task stays play: the other player walks, fights and draws on.
// coop-hischest (Jep, 2026-10-27: "P1 interacting with the big chest makes it show P2 toans perspective again ... P1 also
// stops rendering Xiao during the interaction"): player 1's Cross at those chests runs his own prompt too (DunMoveChara asks
// OpenChestPrompt(1), MenuTick steps it on pad 1, MenuFilterPort keeps his pad from the game meanwhile). Program 15 or 16
// run as the game's task put the floor in event mode (BtSystemScriptInit), which holds and hides the partner on both copies
// (CoopStep) and turns both screens to the script's camera (SubCamera, framing him).
// The gate: her yes asks player 1 beside his running game (CoopWorldsGateAskBeside); both yes, the gate's script runs for
// both (both leave), its escape window answered with her yes; his no asks her to leave anyway, which splits the pair as before.
// DC_COOP_MUTATE=promptpause: her Cross runs the retail scripts, as before (the twin).

#include "coop/game_ns.hpp"
#include "coop/coop.hpp"
#include "coop/coop_lockhud.hpp" // coop-areanotice: LockHudUnderModal
#include "coop/netplay.hpp"
#include "coop/netplay_cmd.hpp" // coop-wave2: kCmdPrompt
#include "coop/coop_ready.hpp" // coop-readyscreen: the ready box
#include "coop/coop_actor.hpp" // coop-readyscreen: each row's member (ActorChara)
#include "coop/coop_town.hpp" // coop-readyscreen: the town partner's member
#include "coop/coop_wheel.hpp" // coop-readyscreen: each row's face (CoopDrawHudFace)

#include <libvu0.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "battlemenu/internal.hpp" // DecodeMesFromBuffer, DecodeItemNamePort
#include "btmisc.hpp"   // BtBattleMusic_Stop
#include "text/caption_text.hpp"
#include "clsmes.hpp"
#include "clsmes.hpp" // coop-floorgate: ClsMesSetChoiceRows
#include "dngmessageman.hpp"
#include "shot_freefuncs.hpp" // BtSetStatusErr
#include "sysmes.hpp"         // ItemGetMes
#include "collisiondata.hpp"
#include "dngstatusdata.hpp"
#include "dun/gameloop.hpp"
#include "dungeoneventdata.hpp"
#include "dungeoneventman.hpp"
#include "dungeonmap.hpp"
#include "dungeonparts.hpp"
#include "edit.hpp"
#include "text/gametext.hpp" // coop-floorgate: the gate question's own text
#include "gfx/gfx.hpp"   // coop-callanchor: UiAnchorScope
#include "localize/localize.hpp" // coop-floorgate: the modal's YES and NO
#include "ui/framework/dialogs.hpp" // coop-gatepopup: the asker's waiting box
#include "ui/framework/buttons.hpp" // coop-swapbtn: the plates' style (PlateProbe)
#include "ui/framework/text.hpp"    // coop-gatepopup: the popup's line widths
#include "app/gameloop.hpp"
#include "itembombeffect.hpp"
#include "itemdata.hpp"
#include "menu_draw.hpp"
#include "menu_dungeon.hpp" // coop-chestrolls: GetAttachVolumeForMsg
#include "menu_save.hpp"    // coop-chestrolls: GetAddAttachItem
#include "mglib.hpp" // coop-gatepopup: MGFillBox, the pause's dim
#include "rect.hpp"
#include "runscript.hpp"
#include "savedata.hpp"
#include "snd.hpp"
#include "userstatus.hpp"

#include "coop/coop_audio.hpp"
#include "coop/coop_combat.hpp"
#include "coop/coop_camera.hpp" // coop-drawrng: the prompt's window draws from the draw stream
#include "coop/coop_core.hpp"
#include "coop/coop_interact.hpp"
#include "coop/coop_inventory.hpp"
#include "coop/coop_worlds.hpp"
#include "coop/coop_message.hpp" // coop-itemprompt
#include "coop/coop_menu.hpp"    // coop-hischest: MenusOn
#include "coop/coop_fullmenu.hpp" // coop-townwheel: FullMenuOpenFor
#include "coop/coop_prompt.hpp"   // coop-townwheel: the swap question

namespace dc::game {
extern s32 gameTask;
extern int mode; // coop-gatepopup2 (3: a dungeon, GAME_MODE_DUNGEON)
extern s16 *ItemSetRateTbl[7]; // coop-chestrolls: the roll's test line (dungeonparts.cpp)
extern int  floorNum[7];
unsigned int PortRandState();
} // namespace dc::game

// coop-swapbtn (tests): the registry's stamps (render/texture_registry.hpp) and the UI's button plates (ui/framework/buttons.cpp).
namespace dc::render {
enum class PortTextureOwner : std::uint8_t;
std::uint64_t PortKeyStamp(unsigned key);
bool          PortKeyInfo(unsigned key, PortTextureOwner *owner, unsigned *width, unsigned *height);
} // namespace dc::render
namespace dc::ui {
dc::game::CTexture *TestButtonPlate(const Rect &box, const ButtonStyle &style, bool make, std::uint64_t *made_stamp);
} // namespace dc::ui


namespace coop {

namespace {

constexpr int kTreasureKey = ITEM_TREASURE_KEY;

enum class Kind { None, Chest, Piero, Gate, Message }; // Message: a floor script's Cross message (coop-gatekeyno)

// What the prompt shows now: a message to read (Cross goes on), or a choice of rows.
enum class Step {
    Read,     // a message; on Cross, then (what follows it)
    Choose,   // rows to choose from
    Waiting,  // the gate: the other player is asked (CoopWorldsStatus draws it)
};

// What a message read leads to.
enum class Then {
    Close,        // nothing more
    MainChoice,   // 15: message 31's choice
    TrapChoice,   // 15: the guess (32, or 40 to 42 with Mardan)
    TrapOff,      // 15: the trap off, then close
    KeySpent,     // 15: the trap off, the key spent
    Poison,       // 15: poison, close
    Bomb,         // 15: a bomb, close
    Curse,        // 15: a curse, close
    PieroChoice,  // 16: message 51's choice
};

// Lines of text in fixed storage (no heap: the prompt is simulation state, in the snapshot's state set like the key picker).
struct Text {
    char line[6][80] = {};
    int  count = 0;

    void clear() { count = 0; }
    int  size() const { return count; }
    bool empty() const { return count == 0; }
    void push_back(const std::string &text) {
        if (count < 6) {
            std::snprintf(line[count++], sizeof line[0], "%s", text.c_str());
        }
    }
    const char *operator[](int i) const { return line[i]; }
    void set(int i, const std::string &text) { std::snprintf(line[i], sizeof line[0], "%s", text.c_str()); }
};

int g_script_choice[2] = {-1, -1}; // coop-choosing: each player's answer to a door script's question (-1: none yet)

struct Prompt {
    Kind                     kind = Kind::None;
    Step                     step = Step::Read;
    Then                     then = Then::Close;
    int                      active = -1; // the chest's map event (NowDngMap->events), or the gate's event slot
    int                      box = -1;    // the chest's boxes[] entry
    CDungeonMap             *map = nullptr;
    int                      mes = -1;    // the message shown
    Text                     head;        // a message's lines, or a choice's question
    Text                     rows;        // a choice's rows
    int                      cursor = 0;
    int                      piero[2] = {-1, -1};
    std::uint16_t            held = 0;
    char                     note[96] = {};
    int                      note_ticks = 0;
    int                      note_mes = -1; // coop-playfix: the note is this message in the game's window (-1: the caption)
    bool                     note_waits = false; // coop-cavehold: the door's message stays, its player held, until their Cross
    // coop-wave2 (talks, questions and prompts local): which prompt and which of its steps this is (every copy alike: set as it opens and
    // as each step's transition runs), and the transition its player's command brought, taken at this player's next step.
    long long                id = -1;
    int                      seq = 0;
    int                      due = 0;
    int                      due_row = 0;
};

Prompt        g_prompts[2];

// coop-wave2 (localaudit 3.5, Jep-approved Wave 2: prompts local): linked together, a prompt's hand (its cursor on a choice's rows or
// the gate's) and the press that moves it on (a read message's Cross, a row's Cross, a Circle, the gate's pick, a town door's message
// closed) are its player's copy's own: that copy turns the press into one command (kCmdPrompt: the transition, its row, which
// prompt and step it was made on), and every copy runs that transition (AfterRead, Chosen, the gate's ask, Close) at that player's
// step on its tick, so the chest's, the clown's, the trap's and the gate's world effects happen on every copy alike, as before, and
// nothing of the hand is shared. What the prompt shows (its step, its text) changes only by those transitions; the buttons held
// (Cur().held) still follow that player's agreed pad on every copy, as the game's buttons after a close read them. Apart, alone and a
// catch-up's replay as before. DC_COOP_MUTATE=prompt_lockstep (twin): stepped from the agreed pads on every copy, as before.
enum PromptAct : int { kActNone = 0, kActRead = 1, kActRow = 2, kActRowCircle = 3, kActGateGo = 4, kActGateStay = 5, kActNoteClose = 6 };
struct PromptOwn {
    long long id = -1;
    int       seq = -1;
    int       cursor = 0;
    bool      sent = false; // its command on its way: no other press till it lands
};
PromptOwn g_prompt_own[2]; // this copy's own, never state
std::uint16_t g_held_after[2]; // coop-hischest: what the user held as the prompt closed, kept from the game until let go
std::uint16_t g_note_held_after[2]; // coop-cavehold: and as a door's message closed in town
int           g_drawn[2]; // frames each prompt was drawn on this copy (draw only, the test's line)
int    g_who = 2;
int    g_escape_answer = 0; // the gate's escape window, answered already (1 the next floor, 2 back to town), once

Prompt &Cur() {
    return g_prompts[g_who - 1];
}

const char *Who() {
    return g_who == 1 ? "player 1's" : "partner's";
}

void Log(const char *line) {
    std::fprintf(stderr, "coop: %s prompt: %s\n", Who(), line);
    std::fflush(stderr);
}

// The floor's script messages (the system script's message file, which BtSystemScriptLoad gave the event's window 1).
std::string Message(int no) {
    ClsMes *window = EdEventInfo.messages[1];

    return window != nullptr && window->buff != nullptr ? dc::battlemenu::detail::DecodeMesFromBuffer(window->buff, no) : std::string();
}

std::vector<std::string> Lines(const std::string &text) {
    std::vector<std::string> lines;
    std::size_t              from = 0;

    while (from <= text.size()) {
        std::size_t to = text.find('\n', from);
        std::string line = text.substr(from, to == std::string::npos ? std::string::npos : to - from);

        line.erase(0, line.find_first_not_of(' ') == std::string::npos ? line.size() : line.find_first_not_of(' '));

        if (!line.empty()) {
            lines.push_back(line);
        }

        if (to == std::string::npos) {
            break;
        }

        from = to + 1;
    }

    return lines;
}

void Note(const char *text) {
    std::snprintf(Cur().note, sizeof Cur().note, "%s", text);
    Cur().note_ticks = 150;
    Cur().note_mes = -1;
    Cur().note_waits = false;
}

void Close(const char *why) {
    char line[160];

    Cur().seq++; // coop-wave2

    std::snprintf(line, sizeof line, "closes (%s), drawn %d frames on this copy", why, g_drawn[g_who - 1]);
    Log(line);
    g_held_after[g_who - 1] = Cur().held;
    Cur().kind = Kind::None;
    Cur().head.clear();
    Cur().rows.clear();
}

void Read(int mes, Then then) {
    std::string text = Message(mes);
    char        line[256];

    Cur().seq++; // coop-wave2
    Cur().step = Step::Read;
    Cur().then = then;
    Cur().mes = mes;
    Cur().head.clear();
    Cur().rows.clear();

    for (const std::string &one : Lines(text)) {
        Cur().head.push_back(one);
    }
    std::snprintf(line, sizeof line, "message %d: %s", mes, text.empty() ? "(no text)" : text.c_str());
    Log(line);
}

// A choice of count rows: the message's last count lines, any before them its question.
void Choose(int mes, int count, Then then) {
    std::vector<std::string> lines = Lines(Message(mes));
    std::string              all;

    Cur().seq++; // coop-wave2
    Cur().step = Step::Choose;
    Cur().then = then;
    Cur().mes = mes;
    Cur().cursor = 0;
    Cur().head.clear();
    Cur().rows.clear();

    while ((int) lines.size() < count) {
        lines.push_back("-");
    }

    for (std::size_t i = 0; i < lines.size(); i++) {
        if (i + count < lines.size()) {
            Cur().head.push_back(lines[i]);
        } else {
            Cur().rows.push_back(lines[i]);
        }

        all += (i ? " | " : "") + lines[i];
    }

    char line[320];

    std::snprintf(line, sizeof line, "choice %d (%d rows): %s", mes, count, all.c_str());
    Log(line);
}

bool BoxStill() {
    if (NowDngMap != Cur().map || Cur().active < 0 || Cur().box < 0) {
        return false;
    }

    const TREASURE_BOX &box = NowDngMap->boxes[Cur().box];

    return NowDngMap->events[Cur().active].kind == DNG_EVENT_TREASURE_BOX && box.closed != 0 &&
           (Cur().kind == Kind::Piero ? box.trap_no == 5 : box.trap_no != 0);
}

// One of item out of pack (the slot set to 0, as the game's item loss leaves it). False when none is there.
bool TakeOne(ITEM_PACK &pack, int item) {
    for (int i = 0; i < pack.num && i < 103; i++) {
        if (pack.item[i] == item) {
            pack.item[i] = 0;
            return true;
        }
    }

    return false;
}

bool Holds(const ITEM_PACK &pack, int item) {
    for (int i = 0; i < pack.num && i < 103; i++) {
        if (pack.item[i] == item) {
            return true;
        }
    }

    return false;
}

// The user's own Treasure Key: her bag (or the party's pack) for player 2, the party's for player 1.
ITEM_PACK *KeyPack() {
    if (g_who == 2 && Holds(PartnerPack(), kTreasureKey)) {
        return &PartnerPack();
    }

    return Holds(UserStatus->item_pack, kTreasureKey) ? &UserStatus->item_pack : nullptr;
}

const float *UserPos() {
    return g_who == 1 ? CharaMain.pos : g_partner.pos;
}

// The trap's harm, on the user, as _SET_STATUS_ERR and _BOM_SET give it (coop_combat.cpp's partner versions).
void Harm(Then then) {
    sceVu0FVECTOR pos;

    sceVu0CopyVector(pos, (float *) UserPos());

    if (then == Then::Bomb) {
        sceVu0FVECTOR push = {0.0f, 0.0f, 1.0f, 1.0f};
        sceVu0FVECTOR direction;

        pos[1] += 16.0f;
        pos[2] -= 3.0f;

        int bomb = SetBombEffect(pos, 1, selectMapNo * 20 + 20, 0.8f);

        if (bomb != -1) {
            sceVu0Normalize(direction, push);
            sceVu0ScaleVectorXYZ(NowColData->hit[bomb].velocity, direction, 0.8f);
        }

        Log("the trap's bomb goes off at the user");
        return;
    }

    int ailment = then == Then::Poison ? AILMENT_POISON : AILMENT_CURSE;

    if (g_who == 2) {
        PartnerSetAilment(ailment);
    } else {
        BtSetStatusErr(ailment);
    }

    pos[1] += 16.0f;
    NowColData->Set(pos, 2, 2, 5.0f, 0.0f, 1, 2, 0, 0);

    char line[96];

    std::snprintf(line, sizeof line, "the trap gives the user ailment %d", ailment);
    Log(line);
}

void TrapOff() {
    NowDngMap->boxes[Cur().box].trap_no = 0;
    Log("the chest's trap is off");
}

// After a message: what it leads to.
void AfterRead() {
    const Then then = Cur().then;

    switch (then) {
        case Then::MainChoice:
            Choose(31, 3, Then::Close);
            return;
        case Then::PieroChoice:
            Choose(51, 2, Then::Close);
            Cur().rows.set(0, dc::battlemenu::detail::DecodeItemNamePort(Cur().piero[0], Cur().rows[0]));
            Cur().rows.set(1, dc::battlemenu::detail::DecodeItemNamePort(Cur().piero[1], Cur().rows[1]));
            return;
        case Then::TrapChoice: {
            const int trap = NowDngMap->boxes[Cur().box].trap_no;

            if (SaveData->GetMardanGareyanFlag() != 0 && trap == 1) {
                Read(43, Then::TrapOff);
            } else {
                Choose(SaveData->GetMardanGareyanFlag() != 0 ? 38 + trap : 32, 4, Then::Close);
            }

            return;
        }
        case Then::TrapOff:
            TrapOff();
            break;
        case Then::KeySpent: {
            ITEM_PACK *pack = KeyPack();

            if (pack == nullptr || !TakeOne(*pack, kTreasureKey)) {
                Note("The key is gone.");
                break;
            }

            TrapOff();
            Log(pack == &UserStatus->item_pack ? "a Treasure Key spent from the party's pack" : "a Treasure Key spent from her own bag");
            break;
        }
        case Then::Poison:
        case Then::Bomb:
        case Then::Curse:
            Harm(then);
            break;
        default:
            break;
    }

    Close("done");
}

// A row chosen.
void Chosen(int row) {
    char line[200];

    std::snprintf(line, sizeof line, "row %d of choice %d", row, Cur().mes);
    Log(line);

    // coop-choosing: a door script's question (ScriptChoicePrompt): its row is the script's answer, read by the door's run.
    if (Cur().kind == Kind::Message) {
        g_script_choice[g_who - 1] = row;
        Close(row == 0 ? "its question answered yes" : "its question answered no");
        return;
    }

    if (Cur().kind == Kind::Piero) {
        int item = Cur().piero[row == 1 ? 1 : 0];

        if (item <= 0) {
            Close("no item");
            return;
        }

        TREASURE_BOX &box = NowDngMap->boxes[Cur().box];

        // coop-bagoverflow (Jep 11-14: match the base game): retail's script 16 gives the pick with _SITEM_GET (editloop3.cpp) =
        // EdGetItem = CDngStatusData::GetItem, which never refuses: a full bag puts it in the three slots past the bag's size and
        // sets overflow_flag, the chest opens (one roll ever) and the player's menu then opens on its throw-away page (movechara:
        // the flag; Btl_InitItemMode / BattleMenuInit: CheckItemThrow). So no room check: player 1's pick through the party's
        // status, player 2's (a bag item: her own bag) through the same routine with her bag in the status, as her menu puts it
        // there (MenuSwapP2Bag); her overflow flag is her bag's (PartnerOverflowTake), so her own menu opens on it.
        // DC_COOP_MUTATE=clown_no_room (twin): a full bag is refused with "There is no room for it." and the chest stays shut,
        // its next Cross a new roll, as before.
        auto      *status = (CDngStatusData *) UserStatus;
        const bool hers = g_who != 1 && OwnBags() && PartnerKeeps(item);

        if (Mutated("clown_no_room") && (hers ? PartnerCheckItemGet(item) : status->CheckItemGet(item)) != 0) {
            Note("There is no room for it.");
            Close("no room");
            return;
        }

        {
            // coop-chestrolls (Jep 11-13: "check if big chest is handling its rolls for King Mimic and the clown event properly"):
            // retail's script 16 gives the pick through _SITEM_GET (editloop3.cpp), which hands a stat attachment (Attack,
            // Endurance, Speed, Magical Power: GetAddAttachItem) over at level 3 and names that in _ITEM_GET_MES; this gave
            // level 0, which SetAttachMentValue makes +1. DC_COOP_MUTATE=piero_level1 (twin): level 0, as before.
            const int level = GetAddAttachItem(item) != 0 && !Mutated("piero_level1") ? 3 : 0;

            MessageBy by(g_who); // coop-itemprompt: on the user's screen only
            // (her bag swapped in brings her overflow flag with it; a party item of hers (a weapon, an attachment) keeps the
            // party's flag aside, so a pick of hers past the room opens her menu, not player 1's)
            const bool swapped = hers && MenuSwapP2Bag();
            // DC_COOP_MUTATE=shared_overflow_party_flag (twin): her pick of a party item past the room sets the party's flag, so
            // player 1's menu opens for it.
            const bool aside = g_who != 1 && !swapped && !Mutated("shared_overflow_party_flag");
            const int  kept = status->overflow_flag;

            if (aside) {
                status->overflow_flag = false;
            }
            const int  slot = status->GetItem(item, level);
            const bool over = status->overflow_flag != 0;

            if (aside) {
                status->overflow_flag = kept;
            }
            if (swapped) {
                MenuSwapP2Bag();
            }
            if (g_who != 1 && over && (swapped || aside)) {
                PartnerOverflowSet(g_who);
            }
            ItemGetMes(item, level > 0 ? level : -1, 0x96, 0);
            DngMessMan.hide_timer = 0x96;
            if (GetAddAttachItem(item) != 0 && slot >= 0) {
                std::snprintf(line, sizeof line, "the clown's attachment %d arrives at +%d (retail's script gives +3)", item,
                              GetAttachVolumeForMsg(reinterpret_cast<ATTACH_LIST *>(&status->consumable_items[slot])));
                Log(line);
            }
            if (over) {
                const ITEM_PACK &pack = hers ? PartnerPack() : status->item_pack;

                std::snprintf(line, sizeof line, "the clown's pick %d goes past the bag's room: overflow %d %d %d (bag of %d), the throw-away asked next (%s)", item,
                              (int) pack.item[pack.num], (int) pack.item[pack.num + 1], (int) pack.item[pack.num + 2], (int) pack.num,
                              hers ? "her own bag" : g_who == 1 ? "the party's status" : "the party's, her flag");
                Log(line);
            }
        }

        box.lid_angle = -30.0f;
        box.closed = 0;
        box.trap_no = 0;
        NowDngMap->events[Cur().active].kind = DNG_EVENT_NONE;
        SndSePlay(SE_CHEST_OPEN_BIG, -1, 0);
        std::snprintf(line, sizeof line, "the clown's chest gives item %d", item);
        Log(line);
        Close("done");
        return;
    }

    // 15's choices.
    if (row == 0) {
        Close("left");
        return;
    }

    if (Cur().mes == 31) {
        if (row == 1) {
            if (KeyPack() == nullptr) {
                Read(33, Then::Close);
            } else {
                Read(35, Then::KeySpent);
            }
        } else if (SaveData->GetMardanGareyanFlag() != 0) {
            Read(39, Then::TrapChoice);
        } else {
            Cur().then = Then::TrapChoice;
            AfterRead();
        }

        return;
    }

    // The guess.
    const int trap = NowDngMap->boxes[Cur().box].trap_no;

    if (trap == 1 || trap == row + 1) {
        Read(34, Then::TrapOff);
    } else if (trap == 2) {
        Read(36, Then::Poison);
    } else if (trap == 3) {
        Read(37, Then::Bomb);
    } else if (trap == 4) {
        Read(38, Then::Curse);
    } else {
        Close("no such trap");
    }
}

bool PromptLocal() {
    return NetplayHasPlayer() && NetplayPartnerLinked() && !CoopWorldsApart() && !NetplayReplaying() && !Mutated("prompt_lockstep");
}

// This copy's own hand on player's prompt (made afresh as the prompt or its step changes, from the step's own start).
PromptOwn &OwnPrompt(int player) {
    PromptOwn     &own = g_prompt_own[player - 1];
    const Prompt  &p = g_prompts[player - 1];

    if (own.id != p.id || own.seq != p.seq) {
        own.id = p.id;
        own.seq = p.seq;
        own.cursor = p.cursor;
        own.sent = false;
    }
    return own;
}

// The cursor a prompt shows on this copy: its player's own hand there, linked together.
int ShownCursor(int player, const Prompt &p) {
    return PromptLocal() && player == NetplayLocalPlayer() ? OwnPrompt(player).cursor : p.cursor;
}

void PromptSend(int player, int act, int row) {
    NetCommand command;

    command.kind = kCmdPrompt;
    command.a = (std::uint8_t) act;
    command.b = (std::uint16_t) row;
    command.c = (std::uint32_t) ((g_prompts[player - 1].id & 0xFFFF) << 16 | (g_prompts[player - 1].seq & 0xFFFF));
    NetplayQueueCommand(command);
    g_prompt_own[player - 1].sent = true;
    std::fprintf(stderr, "coop: player %d's prompt: its press %d (row %d) on its own copy at link tick %lld: sent\n", player, act, row, (long long) NetplayLinkTick());
}

void ApplyPromptCmd(int player, const NetCommand &command, std::int64_t tick) {
    if (player < 1 || player > (int) std::size(g_prompts)) {
        return;
    }
    Prompt         &p = g_prompts[player - 1];
    const bool      note = command.a == kActNoteClose;
    const unsigned  want = (unsigned) ((p.id & 0xFFFF) << 16 | (p.seq & 0xFFFF));

    if ((note ? !p.note_waits : p.kind == Kind::None) || command.c != want) {
        std::fprintf(stderr, "coop: player %d's prompt press %d at link tick %lld finds its prompt moved on: ignored\n", player, (int) command.a, (long long) tick);
        return;
    }
    p.due = command.a;
    p.due_row = command.b;
}

void PromptHandlerOnce() {
    static bool done = false;

    if (!done) {
        done = true;
        NetplaySetCommandHandler(kCmdPrompt, ApplyPromptCmd);
    }
}

// A prompt opened for player (every copy alike): which one it is, for its commands.
void Opened(int player) {
    g_prompts[player - 1].id = NetplayLinkTick();
    g_prompts[player - 1].due = 0;
    PromptHandlerOnce();
}

// coop-hischest: player 1's prompt steps in MenuTick, so only with the co-op menus on (else his Cross runs the script, as
// retail's). DC_COOP_MUTATE=chestpausep1: his Cross runs the script, as before (the twin).
bool Usable(int player) {
    if (player == 1 ? !MenusOn() || Mutated("chestpausep1") : player != 2 || Mutated("promptpause")) {
        return false;
    }

    return (player == 1 ? CoopPlainPlay() : gameTask == GAME_TASK_PLAY) && NowDngMap != nullptr && UserStatus != nullptr && SaveData != nullptr && EdEventInfo.messages[1] != nullptr &&
           EdEventInfo.messages[1]->buff != nullptr;
}

// coop-chestrolls: the clown's roll on stderr, every copy (the game's stream before and after, the two items), and under
// DC_COOP_ROLL_LOG=1 the two lists it drew from with each item's turn-down rate, so a test can run retail's formula
// (GetPieroItem, dungeonparts.cpp) from the state before and compare. DC_COOP_TEST_PIERO_ITEM=N (a test): the second item is N
// after the roll (the level twin needs a stat attachment on offer).
void RollLine(unsigned int before, int entry_no) {
    static const bool lists = TestEnvInt("DC_COOP_ROLL_LOG", 0) != 0;
    static const int  forced = TestEnvInt("DC_COOP_TEST_PIERO_ITEM", -1);
    char              line[200];

    std::snprintf(line, sizeof line, "the clown's roll at box %d: items %d and %d, random %08x before, %08x after, link tick %lld", entry_no,
                  Cur().piero[0], Cur().piero[1], before, PortRandState(), (long long) NetplayLinkTick());
    Log(line);

    if (lists) {
        const PIERO_ITEM_SET *list = PieroItemListPtr[selectMapNo + BtUraDongeon * 7];
        const PIERO_ITEM_SET &set = UserStatus->cur_floor < floorNum[selectMapNo] ? list[0] : list[1];

        for (int n = 0; n < 2; n++) {
            std::string text = "the clown's list " + std::to_string(n) + " (item:rate):";

            for (int i = 0; i < set.count[n] && i < 64; i++) {
                text += " " + std::to_string(set.item[n][i]) + ":" + std::to_string((int) ItemSetRateTbl[selectMapNo][set.item[n][i] - 1]);
            }

            std::fprintf(stderr, "coop: %s prompt: %s\n", Who(), text.c_str());
        }

        std::fflush(stderr);
    }

    if (forced > 0) {
        Cur().piero[1] = forced;
        std::snprintf(line, sizeof line, "test: the clown's second item is %d", forced);
        Log(line);
    }

    // (coop-bagoverflow, x-clown-apart -Full: DC_COOP_TEST_PIERO_FIRST=N, the first item N, the one a Cross alone takes)
    static const int first = TestEnvInt("DC_COOP_TEST_PIERO_FIRST", -1);

    if (first > 0) {
        Cur().piero[0] = first;
        std::snprintf(line, sizeof line, "test: the clown's first item is %d", first);
        Log(line);
    }
}

} // namespace

bool OpenChestPrompt(int player, int active, int entry_no) {
    if (!Usable(player) || g_prompts[player - 1].kind != Kind::None) {
        return false;
    }

    TREASURE_BOX &box = NowDngMap->boxes[entry_no];

    if (box.kind != TREASURE_BOX_LARGE || box.trap_no == 0 || box.closed == 0) {
        return false;
    }

    // coop-chestrolls: one roll per clown chest, as retail's (its Cross takes the chest's event away at once, movechara.cpp, so
    // script 16 and its one _GET_PIERO_ITEM run once per chest). While one player's prompt is open at the chest another's Cross
    // opened a second prompt with a roll of its own: the pair chose from four items. Now that Cross gets a note and no roll.
    // DC_COOP_MUTATE=piero_tworolls (twin): a prompt and a roll each, as before.
    if (box.trap_no == 5 && !Mutated("piero_tworolls")) {
        for (int other = 1; other <= (int) std::size(g_prompts); other++) { // (every other player's)
            const Prompt &open = g_prompts[other - 1];

            if (other != player && open.kind == Kind::Piero && open.map == NowDngMap && open.box == entry_no) {
                g_who = player;
                Note("Someone is already opening it.");
                Log("the clown's chest is another player's now: no prompt, no roll");
                return true;
            }
        }
    }

    g_who = player;
    Cur() = Prompt{};
    Cur().kind = box.trap_no == 5 ? Kind::Piero : Kind::Chest;
    Cur().active = active;
    Cur().box = entry_no;
    Cur().map = NowDngMap;
    Cur().held = kInputCross;
    g_drawn[player - 1] = 0;
    Opened(player); // coop-wave2

    char line[160];

    std::snprintf(line, sizeof line, "opens at chest %d (box %d, trap %d), the game plays on (task %d)", active, entry_no, box.trap_no, (int) gameTask);
    Log(line);
    SoundOwner sound(player);
    SndSePlay(MENU_SOUND_CONFIRM, -1, 0);

    if (Cur().kind == Kind::Piero) {
        const unsigned int before = PortRandState();

        if (Mutated("piero_extradraw")) { // (twin: a draw that is not retail's shifts the roll off its formula)
            rand();
        }

        GetPieroItem(selectMapNo, BtUraDongeon, &Cur().piero[0], &Cur().piero[1]);
        RollLine(before, entry_no);
        Read(50, Then::PieroChoice);
    } else {
        Read(30, Then::MainChoice);
    }

    return true;
}

bool OpenGatePrompt(int player, CDungeonEventData *gate) {
    // coop-gateflow: the gate to the floor menu (coop_worlds' CoopWorldsGatePicker): no prompt, her copy (or both, split) goes.
    if (Usable(player) && g_prompts[player - 1].kind == Kind::None && gate != nullptr && CoopWorldsGatePicker(player, gate)) {
        return true;
    }

    if (!Usable(player) || g_prompts[player - 1].kind != Kind::None || gate == nullptr || !CoopWorldsGateAskable()) {
        return false;
    }

    g_who = player;
    Cur() = Prompt{};
    Cur().kind = Kind::Gate;
    Cur().active = (int) (gate - NowEventMan->event);
    Cur().map = NowDngMap;
    Cur().held = kInputCross;
    Cur().step = Step::Choose;
    g_drawn[player - 1] = 0;
    Opened(player); // coop-wave2
    Cur().head.push_back("Leave this floor?");
    Cur().rows.push_back("Go to the next floor");
    Cur().rows.push_back("Go back to town");
    Cur().rows.push_back("Stay here");

    char line[160];

    std::snprintf(line, sizeof line, "opens at gate %s (script %d), the game plays on (task %d)", gate->event->name, (int) gate->event->script_no,
                  (int) gameTask);
    Log(line);
    SoundOwner sound(player);
    SndSePlay(MENU_SOUND_CURSOR, -1, 0);
    return true;
}

bool CrossMessagePrompt(int player, int script_no) {
    if (!Usable(player)) {
        return false;
    }

    const int mes = CrossMessage(script_no);

    if (mes < 0) {
        return false;
    }

    g_who = player;

    std::string text = Message(mes);
    char        line[256];

    for (char &c : text) {
        c = c == '\n' ? ' ' : c;
    }

    // coop-gatekeyno (Jep's 2026-11-04 pack 8 playtest: the floor lock's "There is a big keyhole. (Use item with )" came up in a
    // navy box of ours, its button picture missing, over the game's "!"): the message in the game's own message window (PutWindow,
    // as a chest's), its button picture and all, on this player's screen only, as retail's script shows it and waits: up until
    // the player's Cross or Circle, that player standing (a prompt holds its player), the other playing on.
    // DC_COOP_MUTATE=crossmes_caption (twin): our caption box for 150 ticks, as before.
    if (!text.empty() && !Mutated("crossmes_caption")) {
        Cur() = Prompt{};
        Cur().kind = Kind::Message;
        Cur().map = NowDngMap;
        Cur().held = kInputCross; // (the Cross that opened it, let go first)
        g_drawn[player - 1] = 0;
        Opened(player); // coop-wave2
        Read(mes, Then::Close);
    } else {
        Note(text.empty() ? "Nothing happens." : text.c_str());
    }
    std::snprintf(line, sizeof line, "script %d's Cross shows message %d only, on this player's screen: %s", script_no, mes, text.c_str());
    Log(line);
    SoundOwner sound(player);
    SndSePlay(MENU_SOUND_CURSOR, -1, 0);
    return true;
}

// coop-choosing (Jep 11-12: a door's script runs for its user beside the game): a message a door's script shows as it runs beside
// the game (coop_interact's door interpreter, _MES_MAKE: Moon Sea's plank, "ita"), in the game's own message window on that
// player's screen only, as CrossMessagePrompt's, up until that player's Cross; the others play on. False when that player has a
// prompt up already or none can open (then the message is not shown; the door's steps go on).
bool ScriptMessagePrompt(int player, int mes) {
    if (!Usable(player) || g_prompts[player - 1].kind != Kind::None || mes < 0 || Message(mes).empty()) {
        return false;
    }

    g_script_choice[player - 1] = -1;

    g_who = player;
    Cur() = Prompt{};
    Cur().kind = Kind::Message;
    Cur().map = NowDngMap;
    Cur().held = kInputCross; // (the Cross of the pick that started the door, let go first)
    g_drawn[player - 1] = 0;
    Opened(player); // coop-wave2
    Read(mes, Then::Close);

    char line[96];

    std::snprintf(line, sizeof line, "a door's script beside the game shows message %d, on this player's screen only", mes);
    Log(line);
    return true;
}

// coop-choosing (ura): the script's message up as this player's prompt turns into its question (the script set the window's cursor:
// retail's choice helper, event.stb's function at 16820): its rows the message's last two lines, row 0 retail's default. The
// answer (ScriptPromptChoice) is read back by the door's run. False when no such message is up.
bool ScriptChoicePrompt(int player) {
    g_who = player;

    if (player < 1 || player > 2 || Cur().kind != Kind::Message || Cur().step != Step::Read) {
        return false;
    }

    g_script_choice[player - 1] = -1;
    Choose(Cur().mes, 2, Then::Close);
    g_drawn[player - 1] = 0;
    Log("the door's message is its question: a choice on this player's screen only");
    return true;
}

// Test hook (x-back-half -Leave): the player's door question answered with row at once, on every linked copy alike.
bool ScriptPromptTestAnswer(int player, int row) {
    g_who = player;
    if (player < 1 || player > 2 || Cur().kind != Kind::Message) {
        return false;
    }
    if (Cur().step == Step::Read) { // (a message read: closed as its player's Cross closes it)
        AfterRead();
        return true;
    }
    if (Cur().step != Step::Choose) {
        return false;
    }
    Chosen(row);
    return true;
}

int ScriptPromptChoice(int player) {
    return player >= 1 && player <= 2 ? g_script_choice[player - 1] : -1;
}

// coop-menucross: a town door's message for the player's Cross while the door is shut, as that player's own note (drawn on
// that player's copy alone, DrawUsePrompts), the town's script messages (window 1 holds the map script's, as on a floor).
bool TownNotePrompt(int player, int mes) {
    if (player < 1 || player > 2 || EdEventInfo.messages[1] == nullptr || EdEventInfo.messages[1]->buff == nullptr) { // (coop-doorfix: either player)
        return false;
    }

    g_who = player;

    std::string text = Message(mes);
    char        line[256];

    for (char &c : text) {
        c = c == '\n' ? ' ' : c;
    }

    Note(text.empty() ? "Nothing happens." : text.c_str());
    // coop-playfix (Jep, 2026-10-31: the cave door's "A big key hole. (use item with )" box "should never show up over the
    // game's built in UI"): the door's own message in the game's message window (PutWindow, as a chest's), its button picture
    // and all, on this player's screen only. DC_COOP_MUTATE=doornote_caption (twin): the caption box, as before.
    if (!text.empty() && !Mutated("doornote_caption")) {
        Cur().note_mes = mes;
    }
    // coop-cavehold (Jep's 2026-11-02 playtest: "a big key hole." let Toan walk on and went after a second unasked): retail's
    // event shows message 300 and waits for its Cross, its player held. So does the note in the game's window: up, its player
    // standing (the game's pad held for the walker, her walk for her), until their next Cross (StepTownNote); the other plays
    // on. DC_COOP_MUTATE=doornote_timed (twin): its time runs out and nobody is held, as before.
    if (Cur().note_mes >= 0 && !Mutated("doornote_timed")) {
        Cur().note_waits = true;
        Cur().held = kInputCross; // (the Cross that opened it, let go first)
        Opened(player); // coop-wave2
        Cur().seq++;
    }
    std::snprintf(line, sizeof line, "the town door's message %d, on this player's screen only%s%s: %s", mes, Cur().note_mes >= 0 ? " (the game's window)" : "",
                  Cur().note_waits ? ", holds this player until their Cross" : "", text.c_str());
    Log(line);
    SoundOwner sound(player);
    SndSePlay(MENU_SOUND_CURSOR, -1, 0);
    return true;
}

// The notes' time in town (no prompt opens there; the floor's walk steps them on a floor). A waiting door message has none.
void StepTownNotes() {
    for (Prompt &p : g_prompts) {
        if (p.kind == Kind::None && p.note_ticks > 0 && !p.note_waits) {
            p.note_ticks--;
        }
    }
}

// coop-cavehold: a waiting door message, each town tick on its player's agreed pad (both copies): their Cross closes it, kept
// from the game until let go. The pair apart or out of town (a split, a merge: the copies start again from one state) closes
// it too. True while it holds the player.
bool StepTownNote(int player, const InputPadState &pad, bool in_town) {
    g_who = player;
    g_note_held_after[player - 1] &= pad.buttons;

    if (!Cur().note_waits) {
        return false;
    }

    const std::uint16_t pressed = pad.buttons & ~Cur().held;
    const bool          local = PromptLocal(); // coop-wave2: the Cross that closes it is its player's copy's, a command
    const bool          due = Cur().due == kActNoteClose;
    const char         *why = !in_town || !CoopWorldsLinkedTogether() ? "the town left or the pair apart"
                              : local                                ? (due ? "their Cross (its command)" : nullptr)
                              : (pressed & kInputCross) != 0         ? "their Cross"
                                                                     : nullptr;

    Cur().held = pad.buttons;
    Cur().due = 0;
    if (local && why == nullptr && (pressed & kInputCross) != 0 && player == NetplayLocalPlayer() && !OwnPrompt(player).sent) {
        PromptSend(player, kActNoteClose, 0);
    }
    if (why == nullptr) {
        return true;
    }
    Cur().seq++;

    char line[160];

    std::snprintf(line, sizeof line, "the town door's message %d closes on %s at link tick %lld", Cur().note_mes, why, (long long) NetplayLinkTick());
    Log(line);
    g_note_held_after[player - 1] = pad.buttons;
    Cur().note_waits = false;
    Cur().note_ticks = 0;
    Cur().note_mes = -1;
    return false;
}

// Both copies at a split (SplitTo): a waiting message closes, whoever's it was (the leaver's copy leaves the town with it).
void TownNotesEnd() {
    for (int player = 1; player <= 2; player++) {
        g_who = player;
        if (Cur().note_waits) {
            char line[128];

            std::snprintf(line, sizeof line, "the town door's message %d closes on the split at link tick %lld", Cur().note_mes, (long long) NetplayLinkTick());
            Log(line);
            Cur().note_waits = false;
            Cur().note_ticks = 0;
            Cur().note_mes = -1;
        }
    }
}

bool TownNoteHolds(int player) {
    return player >= 1 && player <= 2 && g_prompts[player - 1].note_waits;
}

std::uint16_t TownNoteHeldAfter(int player) {
    return player >= 1 && player <= 2 ? g_note_held_after[player - 1] : 0;
}

bool TownNoteShown(int player) {
    return player >= 1 && player <= 2 && g_prompts[player - 1].kind == Kind::None && g_prompts[player - 1].note_ticks > 0;
}

bool UsePromptOpen(int player) {
    return g_prompts[player - 1].kind != Kind::None;
}

std::uint16_t UsePromptHeldAfter(int player) {
    return g_held_after[player - 1];
}

bool StepUsePrompt(int player, const InputPadState &pad) {
    g_who = player;

    std::uint16_t pressed = pad.buttons & ~Cur().held;

    Cur().held = pad.buttons;
    g_held_after[player - 1] &= pad.buttons;

    if (Cur().note_ticks > 0) {
        Cur().note_ticks--;
    }

    if (Cur().kind == Kind::None) {
        return false;
    }

    // Knocked down, or the floor or the object changed under it (the other player opened it first): it closes.
    if ((player == 2 && g_partner.down > 0) || NowDngMap != Cur().map) {
        Close("down or a new floor");
        return false;
    }

    if ((Cur().kind == Kind::Chest || Cur().kind == Kind::Piero) && !BoxStill()) {
        Note("Someone got there first.");
        Close("the chest changed");
        return false;
    }

    SoundOwner sound(player);

    // coop-wave2: linked together, the press is this player's copy's: its command's transition runs here on every copy alike, and the hand
    // is that copy's own (the others take no press of it).
    const bool local = PromptLocal();
    int        act = kActNone, row = 0;

    if (local) {
        act = Cur().due;
        row = Cur().due_row;
        Cur().due = 0;
        if (act == kActNone && (player != NetplayLocalPlayer() || OwnPrompt(player).sent)) {
            pressed = 0;
        }
    }
    int &cursor = local ? OwnPrompt(player).cursor : Cur().cursor;

    if (Cur().kind == Kind::Gate) {
        if (Cur().step == Step::Waiting) {
            if (!CoopWorldsGateAsking()) {
                Close("the vote is over");
                return false;
            }

            return true;
        }

        if ((pressed & kInputDown) != 0) {
            cursor = (cursor + 1) % 3;
            SndSePlay(MENU_SOUND_CURSOR, -1, 0);
        } else if ((pressed & kInputUp) != 0) {
            cursor = (cursor + 2) % 3;
            SndSePlay(MENU_SOUND_CURSOR, -1, 0);
        }

        bool yes = (pressed & kInputCross) != 0 && cursor < 2; // the escape window's rows: 1 the next floor, 2 back to town

        if (local && act == kActNone) { // (this copy's player's press, a command)
            if ((pressed & kInputCircle) != 0 || ((pressed & kInputCross) != 0 && !yes)) {
                PromptSend(player, kActGateStay, cursor);
            } else if (yes) {
                PromptSend(player, kActGateGo, cursor);
            }
            return true;
        }
        if (local) {
            Cur().cursor = row;
            yes = act == kActGateGo;
        }

        if ((!local && ((pressed & kInputCircle) != 0 || ((pressed & kInputCross) != 0 && !yes))) || act == kActGateStay) {
            SndSePlay(MENU_SOUND_REFUSE, -1, 0);
            Close("stays");
            return true;
        }

        if (yes) {
            SndSePlay(MENU_SOUND_CONFIRM, -1, 0);

            CDungeonEventData &gate = NowEventMan->event[Cur().active];

            if (gate.event == nullptr || !gate.enabled || !CoopWorldsGateAskBeside(player - 1, &gate, Cur().cursor + 1)) {
                Close("the gate cannot be asked now");
                return true;
            }

            Cur().step = Step::Waiting;
            Log(Cur().cursor == 0 ? "next floor: the other player is asked, beside his running game" : "back to town: the other player is asked, beside his running game");
        }

        return true;
    }

    if (Cur().step == Step::Read) {
        if (local && act == kActNone) {
            if ((pressed & (kInputCross | kInputCircle)) != 0) {
                PromptSend(player, kActRead, 0);
            }
            return true;
        }
        if ((!local && (pressed & (kInputCross | kInputCircle)) != 0) || act == kActRead) {
            SndSePlay(MENU_SOUND_CURSOR, -1, 0);
            AfterRead();
        }

        return true;
    }

    const int count = (int) Cur().rows.size();

    if ((pressed & kInputDown) != 0) {
        cursor = (cursor + 1) % count;
        SndSePlay(MENU_SOUND_CURSOR, -1, 0);
    } else if ((pressed & kInputUp) != 0) {
        cursor = (cursor + count - 1) % count;
        SndSePlay(MENU_SOUND_CURSOR, -1, 0);
    }

    if (local && act == kActNone) { // (this copy's player's press, a command)
        if ((pressed & kInputCross) != 0) {
            PromptSend(player, kActRow, cursor);
        } else if ((pressed & kInputCircle) != 0 && Cur().kind == Kind::Message) {
            PromptSend(player, kActRowCircle, 1);
        } else if ((pressed & kInputCircle) != 0 && Cur().kind == Kind::Chest) {
            PromptSend(player, kActRowCircle, 0);
        }
        return true;
    }
    if (local) { // the command's row, on every copy alike
        Cur().cursor = row;
        if (act == kActRow) {
            SndSePlay(MENU_SOUND_CONFIRM, -1, 0);
        } else if (act == kActRowCircle) {
            SndSePlay(MENU_SOUND_REFUSE, -1, 0);
        }
        if (act == kActRow || act == kActRowCircle) {
            Chosen(row);
        }
        return true;
    }

    if ((pressed & kInputCross) != 0) {
        SndSePlay(MENU_SOUND_CONFIRM, -1, 0);
        Chosen(Cur().cursor);
    } else if ((pressed & kInputCircle) != 0 && Cur().kind == Kind::Message) {
        SndSePlay(MENU_SOUND_REFUSE, -1, 0);
        Chosen(1); // coop-choosing: Circle at a door's question is its No (row 1)
    } else if ((pressed & kInputCircle) != 0 && Cur().kind == Kind::Chest) {
        SndSePlay(MENU_SOUND_REFUSE, -1, 0);
        Chosen(0); // row 0 leaves
    }

    return true;
}

bool EscapeWindowAnswer(int *answer) {
    // coop-floorunlock: DC_WORLDS_TEST_ESCAPE=N (both copies alike): the first window opened while linked is answered N (2: back
    // to town), as the gate's user's choice would (x-floor-unlock).
    static const int test_answer = TestEnvInt("DC_WORLDS_TEST_ESCAPE", 0);
    static bool      test_used = false;

    if (g_escape_answer == 0 && test_answer > 0 && !test_used && !CoopWorldsApart() && NetplayHasPlayer()) {
        test_used = true;
        g_escape_answer = test_answer;
    }

    if (g_escape_answer == 0) {
        return false;
    }

    *answer = g_escape_answer;
    g_escape_answer = 0;
    std::fprintf(stderr, "coop: the gate's escape window answered %d already (the players said yes), no window\n", *answer);
    std::fflush(stderr);
    return true;
}

void SetEscapeWindowAnswer(int answer) {
    g_escape_answer = answer;
}

// coop-dungeonreds (x-dungeon-descent-menu-p2: OUT OF SYNC at tick 540, melt 20 0 1 against 0 0 1, heal 192 against 169): her gate's
// prompt waits (Step::Waiting) while he is asked; his yes splits the pair for their floor menus on that tick, so the floor is gone
// before her step's "the vote is over" can close it, and NowDngMap (one object for every floor) never told a new floor either. Her
// copy kept it open across the floor menu; player 1's copy lost it in his leave checkpoint's session reset (netplay Checkpoint), and
// the prompts are no part of the state set, so the next floor's merge left it on her copy alone. There, with his menu open, the
// pause rule read her as in her prompt and held the world (coop_pause PauseZoneHeld) while his copy ran it: the floor's step
// (motionDrive's heal counter, the bag's melt timer) went on one copy only. A player leaving the floor closes their prompt on every
// copy at the split. DC_WORLDS_MUTATE=split_prompt_kept (twin): kept, as before.
void UsePromptEndForLeaver(int player) {
    if (player < 1 || player > (int) std::size(g_prompts) || g_prompts[player - 1].kind == Kind::None) {
        return;
    }
    if (CoopWorldsMutated("split_prompt_kept")) { // (the twin's own line: its stop line)
        std::fprintf(stderr, "coop: test: player %d's prompt kept at the split (split_prompt_kept)\n", player);
        std::fflush(stderr);
        return;
    }
    const int was = g_who;

    g_who = player;
    Close("its player left the floor");
    g_who = was;
}

// coop-clownchest (rc4 owed: a clown chest opened apart reaching the other copy at the merge; x-clown-apart -Case open): apart on a
// floor her copy's game player is her member, so a chest, gate or message she opens there is player 1's prompt on her copy (its
// own pad, its own give). At her floor merge her member becomes the partner (CoopWorldsFloorBecomePartner) but the prompt stayed
// in player 1's slot: her Cross after the merge found "another player's" prompt at her own clown chest, and the arriving copy,
// which took her state without the prompts at all (not in the state set), opened a new one and rolled again: OUT OF SYNC, the
// resync left the floor and her pick was never given. The prompts are in the state set now (build\snapshot-allow.txt) and her
// prompt moves to player 2's slot here, before the set is taken. DC_COOP_MUTATE=useprompt_slot_kept (twin): left in player 1's
// slot, as before.
void UsePromptToPartner() {
    if (g_prompts[0].kind == Kind::None || Mutated("useprompt_slot_kept")) {
        return;
    }

    g_prompts[1] = g_prompts[0];
    g_prompts[0] = Prompt{};
    g_held_after[1] = g_held_after[0];
    g_held_after[0] = 0;
    g_note_held_after[1] = g_note_held_after[0];
    g_note_held_after[0] = 0;
    g_drawn[1] = g_drawn[0];
    g_drawn[0] = 0;
    g_who = 2;
    Log("goes on as player 2's (her floor merge: her member becomes the partner)");
}

// coop-clownchest: DC_COOP_MUTATE=useprompt_not_carried (twin): a state set's apply leaves this copy's own prompts, as when they
// were outside the set (StateSetApplyCore calls these around the apply).
namespace {
Prompt        g_kept_prompts[2];
std::uint16_t g_kept_held[2], g_kept_note_held[2];
bool          g_kept = false;
} // namespace

void UsePromptApplyBefore() {
    g_kept = Mutated("useprompt_not_carried");
    if (g_kept) {
        std::copy(std::begin(g_prompts), std::end(g_prompts), std::begin(g_kept_prompts));
        std::copy(std::begin(g_held_after), std::end(g_held_after), std::begin(g_kept_held));
        std::copy(std::begin(g_note_held_after), std::end(g_note_held_after), std::begin(g_kept_note_held));
    }
}

void UsePromptApplyAfter() {
    if (g_kept) {
        std::copy(std::begin(g_kept_prompts), std::end(g_kept_prompts), std::begin(g_prompts));
        std::copy(std::begin(g_kept_held), std::end(g_kept_held), std::begin(g_held_after));
        std::copy(std::begin(g_kept_note_held), std::end(g_kept_note_held), std::begin(g_note_held_after));
        g_kept = false;
        std::fprintf(stderr, "coop: test: the state set's prompts not taken (useprompt_not_carried): this copy keeps its own\n");
    }
}

void UsePromptResetForSession() {
    g_prompts[0] = Prompt{};
    g_prompts[1] = Prompt{};
    g_held_after[0] = g_held_after[1] = 0;
    g_note_held_after[0] = g_note_held_after[1] = 0;
    g_escape_answer = 0;
}

// The message a script's Cross (action mode 1) shows and nothing else, read from its bytecode: the program opens with
// _GET_ACTION_MODE (1020) compared with 1, and that branch is _SET_MES_SHIPPO (196), one call (window 1, the message, 8, 0,
// 0: the floor script's "show and wait" function) and its return. -1 for any other program.
int CrossMessage(int script_no) {
    const auto *prog = reinterpret_cast<const RS_PROG_HEADER *>(BtSystemScriptFileBuffer.base);

    if (prog == nullptr || script_no < 0 || std::memcmp(prog, "STB", 3) != 0) {
        return -1;
    }

    const char        *base = reinterpret_cast<const char *>(prog);
    const RS_PROGDATA *entry = reinterpret_cast<const RS_PROGDATA *>(base + prog->prog);
    const vmcode_t    *pc = nullptr;

    for (int i = 0; i < prog->prog_num && i < 256; i++) {
        if (entry[i].no == script_no) {
            int addr = 0;

            std::memcpy(&addr, base + entry[i].func, sizeof addr);
            pc = reinterpret_cast<const vmcode_t *>(base + prog->code + addr);
        }
    }

    if (pc == nullptr) {
        return -1;
    }

    // coop-skeldoor (Jep's 2026-10-28 online run: her Cross at the skeleton door blacked Toan's screen, "Xiao is choosing...",
    // and showed her his camera): a program may be a stub that calls its function (the key doors' 250 and 251 are), so its
    // Cross branch is read from that function. DC_COOP_MUTATE=crossstub (twin): the stub's own code only, as before.
    if (pc->op == RS_OP_CALL && !Mutated("crossstub")) {
        int addr = 0;

        std::memcpy(&addr, base + prog->code + pc->arg2, sizeof addr);
        pc = reinterpret_cast<const vmcode_t *>(base + prog->code + addr);
    }

    std::vector<int> pushed;
    int              stage = 0; // 0 the action mode's read, 1 its compare with 1, 2 inside the branch, 3 after the call
    int              mes = -1;

    for (int n = 0; n < 40; n++, pc++) {
        switch (pc->op) {
            case RS_OP_PUSH_CONST:
                pushed.push_back(pc->arg1 == 1 ? pc->arg2 : -1000000);
                break;
            case RS_OP_LOAD:
            case RS_OP_LOAD_ADDR:
                pushed.push_back(-1000000);
                break;
            case RS_OP_POP:
            case RS_OP_STORE:
                break;
            case RS_OP_EXT: {
                int at = (int) pushed.size() - pc->arg1;
                int id = at >= 0 && pc->arg1 > 0 ? pushed[at] : -1;

                if (stage == 0 && id == 1020) {
                    stage = 1;
                } else if (stage != 2 || id != 196) {
                    return -1;
                }

                pushed.resize(std::max(0, at));
                break;
            }
            case RS_OP_CMP:
                if (stage == 1 && pc->arg1 == RS_CMP_EQ && !pushed.empty() && pushed.back() == 1) {
                    stage = 2;
                } else if (stage != 1) {
                    return -1;
                }

                pushed.resize(pushed.size() >= 2 ? pushed.size() - 2 : 0);
                pushed.push_back(-1000000);
                break;
            case RS_OP_JMP_FALSE:
                if (stage != 2) {
                    return -1;
                }

                pushed.clear();
                break;
            case RS_OP_CALL: {
                std::size_t k = pushed.size();

                if (stage != 2 || mes >= 0 || k < 5 || pushed[k - 5] != 1 || pushed[k - 4] < 0 || pushed[k - 3] != 8) {
                    return -1;
                }

                mes = pushed[k - 4];
                pushed.clear();
                pushed.push_back(-1000000);
                stage = 3;
                break;
            }
            case RS_OP_RET:
                return stage == 3 ? mes : -1;
            default:
                return -1;
        }
    }

    return -1;
}

// coop-menucross: what a town door event's Cross shows while its door is shut, read from its bytecode (Norune's cave door,
// event 130: _GET_PADDOWN, then _SGET_GAME_FLAG (701) of the door's flag, and with it clear the Cross branch is
// _SET_MES_SHIPPO and one call (window 1, the message, 8, 0, 0: the script's "show and wait"), message 300). The message and
// the flag, or -1 when the program is not shaped so.
int TownDoorShutMessage(const void *program, int event_no, int *flag) {
    const auto *prog = static_cast<const RS_PROG_HEADER *>(program);

    *flag = -1;

    if (prog == nullptr || event_no <= 0 || std::memcmp(prog, "STB", 3) != 0) {
        return -1;
    }

    const char     *base = reinterpret_cast<const char *>(prog);
    const auto     *entry = reinterpret_cast<const RS_PROGDATA *>(base + prog->prog);
    const vmcode_t *pc = nullptr;

    for (int i = 0; i < prog->prog_num && i < 512; i++) {
        if (entry[i].no == event_no) {
            int addr = 0;

            std::memcpy(&addr, base + entry[i].func, sizeof addr);
            pc = reinterpret_cast<const vmcode_t *>(base + prog->code + addr);
        }
    }

    if (pc == nullptr) {
        return -1;
    }

    std::vector<int> pushed;

    for (int n = 0; n < 80; n++, pc++) {
        switch (pc->op) {
            case RS_OP_PUSH_CONST:
                pushed.push_back(pc->arg1 == 1 ? pc->arg2 : -1000000);
                break;
            case RS_OP_LOAD:
            case RS_OP_LOAD_ADDR:
                pushed.push_back(-1000000);
                break;
            case RS_OP_EXT: {
                const int at = (int) pushed.size() - pc->arg1;

                if (at >= 0 && pc->arg1 >= 3 && pushed[at] == 701 && *flag < 0) {
                    *flag = pushed[at + 1];
                }

                pushed.clear();
                break;
            }
            case RS_OP_CALL: {
                const std::size_t k = pushed.size();

                if (*flag >= 0 && k >= 5 && pushed[k - 5] == 1 && pushed[k - 4] >= 0 && pushed[k - 3] == 8) {
                    return pushed[k - 4];
                }

                pushed.clear();
                break;
            }
            case RS_OP_END:
                return -1;
            default:
                pushed.clear();
                break;
        }
    }

    return -1;
}

namespace {

// coop-hischest (Jep, 2026-10-27: "P2 gets a custom textbox for interacting with the big chest instead of just having her own
// version of the native one"): a chest's messages and choices are drawn as the floor's script draws them, in a message window
// of the user's own made as the script makes its window 1 (BtEventMes0; programs 15 and 16 call _SET_MES_SHIPPO 1 0, _MES_MAKE,
// _SET_MES_POS 8 and _SET_MES_AUTOSET, and for a choice _SET_MES_DRAWSPEED 0 and _SET_MES_CURSOR on the row). The window is
// draw state, on the user's copy only: MakeMesWin's bubble draws rand, which is put back.
// DC_COOP_MUTATE=promptcaption: the caption box, as before (the twin).
struct Window {
    ClsMes                     mes;
    int                        made = -1; // the message laid out, -1 none
    alignas(64) unsigned char tex[0x100] = {};
};

Window g_windows[2];

// coop-keylook: the game's window for the mod's own text (DrawTextInGameWindow); DC_COOP_MUTATE=keylook_box (twin): our navy boxes.
Window                 g_text_windows[4];
dc::text::GameTextFile g_text_files[4];
std::string            g_text_laid[4];

void PutWindow(Window &w, const Prompt &p, int player, int local) {
    if (w.made != p.mes) {
        DrawRngEnter(); // coop-drawrng: the draw stream
        int                at[4] = {0, 0, 0, 0};

        w.mes = *EdEventInfo.messages[1];
        w.mes.tex_buff = w.tex;
        w.mes.mes_made = -1;
        w.mes.wait = 0;
        w.mes.tail_on = 0;
        w.mes.cursor_row = -1;
        w.mes.cursor_y = 0;
        w.mes.text_rate = p.step == Step::Choose ? 0.0f : w.mes.text_rate_set;
        w.mes.MakeMesWin(p.mes);
        // Couch: his on the left, hers on the right.
        w.mes.auto_pos = local != 0 ? MES_POS_BOTTOM : player == 1 ? MES_POS_BOTTOM_LEFT : MES_POS_BOTTOM_RIGHT;
        w.mes.AutoSet(at);
        DrawRngLeave();
        w.made = p.mes;
    }

    w.mes.cursor_row = p.step == Step::Choose ? ShownCursor(player, p) : -1; // coop-wave2: its player's own hand
}

void DrawWindow(Window &w) {
    DrawRngEnter(); // coop-drawrng: the draw stream

    w.mes.Step();
    w.mes.DrawMesWin();
    DrawRngLeave();
    if (static const bool trace = TestEnvOn("DC_COOP_TEXTWIN_TRACE"); trace) { // coop-keylook: where each game window is drawn
        std::fprintf(stderr, "coop: game window drawn at %d,%d (%s) window %d,%d %dx%d place %d bubble %d%c", (int) w.mes.text_x, (int) w.mes.text_y,
                     w.made >= 0 ? "a game message" : "our text", (int) w.mes.win_x, (int) w.mes.win_y, (int) w.mes.win_width, (int) w.mes.win_height,
                     (int) w.mes.auto_pos, (int) w.mes.fukidashi, 10);
    }
}

} // namespace

bool DrawTextInGameWindow(int slot, int player, int local, const std::string &text, int head, int cursor) {
    if (Mutated("keylook_box") || slot < 0 || slot >= 4 || text.empty() || EdEventInfo.messages[1] == nullptr) {
        return false;
    }

    Window     &w = g_text_windows[slot];
    const bool  rows = cursor >= 0;

    if (g_text_laid[slot] != text) {
        DrawRngEnter(); // coop-drawrng: the draw stream
        int at[4] = {0, 0, 0, 0};

        g_text_files[slot].Set(0, text.c_str());
        w.mes = *EdEventInfo.messages[1];
        w.mes.tex_buff = w.tex;
        w.mes.SetBuff(g_text_files[slot].Data());
        w.mes.mes_made = -1;
        w.mes.wait = 0;
        w.mes.tail_on = 0;
        w.mes.cursor_row = -1;
        w.mes.cursor_y = 0;
        w.mes.text_rate = rows ? 0.0f : w.mes.text_rate_set;
        w.mes.MakeMesWin(0);
        // Couch: his on the left, hers on the right (as PutWindow's).
        w.mes.auto_pos = local != 0 ? MES_POS_BOTTOM : player == 1 ? MES_POS_BOTTOM_LEFT : MES_POS_BOTTOM_RIGHT;
        w.mes.AutoSet(at);
        DrawRngLeave();
        g_text_laid[slot] = text;
        if (static const bool trace = TestEnvOn("DC_COOP_TEXTWIN_TRACE"); trace) {
            std::fprintf(stderr, "coop: text window %d (player %d) in the game's window: \"%s\" at %d,%d, %d head line(s)\n", slot, player, text.c_str(),
                         (int) w.mes.text_x, (int) w.mes.text_y, head);
        }
    }

    w.mes.cursor_row = rows ? head + cursor : -1;
    if (rows) {
        ClsMesSetChoiceRows(&w.mes, head);
    }
    DrawWindow(w);
    if (rows) {
        ClsMesSetChoiceRows(nullptr, 0);
    }
    if (static const bool trace = TestEnvOn("DC_COOP_TEXTWIN_TRACE"); trace) {
        std::fprintf(stderr, "coop: text window %d drawn: at %d,%d row %d\n", slot, (int) w.mes.text_x, (int) w.mes.text_y, (int) w.mes.cursor_row);
    }
    return true;
}

namespace {

// coop-floorgate (Jep, 2026-10-28: "use the in game UI ... just text and a yes no option you can pick with the hand cursor
// pointing at the option", then "a popup in the middle of the screen with two button looking options for yes and no", the
// menu's Go Outside question without its gilda and Escape Powder panels, and "you should basically be reusing it not
// rebuilding it"): the gathered gate's question, and the call, drawn by the very call Chronicle's dungeon menu draws that
// question with (port/src/battlemenu/move_menu.cpp's DrawMenuMove: dc::ui::ConfirmModal::Draw, the dark window with the title and
// description, the terracotta YES and NO buttons, the selection frame and the hand easing to the chosen one), on the asked
// player's copy. (The decomp's BattleMenuDraw / DrawDngYesNoDialog are not what runs: Chronicle replaces that draw.)
// DC_COOP_MUTATE=gatherbubble: the floor script's message window instead, the hand at its rows (PutWindow's way, as first built).
constexpr int kGatherHead = 3; // the question's lines above its two rows (the message window's)

Window       g_gather_window;
dc::text::GameTextFile g_gather_text;
dc::ui::Cursor   g_popup_cursor; // the modal's hand (move_menu.cpp keeps one the same way)
int          g_gather_key = -1; // the text laid out (asker, dungeon, floor, why), -1 none
int          g_gather_drawn = 0;

// coop-gatepopup (Jep, 2026-10-28: "the background should dim like it does when you actually press pause"): under a gate's
// question or the asker's waiting box, the town pause's own dim (DrawSysGra: MGFillBox over the screen, black at 0x40), on
// that copy only. DC_COOP_MUTATE=question_dim_off (twin): no dim.
void DimForQuestion() {
    static int dimmed = 0;
    // (coop-warmreset, WS7: put back at each warm-pair job as a fresh copy has it; trace_once_kept twin: kept)
    COOP_TEST_TRACE_RESET(dimmed, 0);

    if (Mutated("question_dim_off")) {
        return;
    }

    CRect_i_ fade;

    fade.x = 0;
    fade.y = 0;
    fade.width = 0x2800;
    fade.height = SCREEN_HALF_HEIGHT << 4;
    MGFillBox(fade, 0, 0, 0, 0x40);
    coop::LockHudUnderModal(); // coop-areanotice: no lock marker over the box
    if (dimmed++ % 600 == 0) {
        std::fprintf(stderr, "coop: question dims the screen (the pause's fill), %d frames\n", dimmed);
        std::fflush(stderr);
    }
}

// coop-gatepopup: how the modal lays a question out (its WrapText at 280, the title and the description apart): each line's
// width, the widest, and the lines it draws against the lines written ("fits" when the modal breaks nowhere else). The game's
// random numbers kept (a layout on the asked copy alone). Returns the widest, -1 when wrapped.
constexpr int kPopupWrap = 280;

int PopupFit(const char *question) {
    DrawRngEnter(); // coop-drawrng: the draw stream
    std::vector<std::string> lines = Lines(question);
    std::string              widths, title, description;
    int                      widest = 0;

    for (const std::string &line : lines) {
        dc::ui::Label label;

        label.KeepGameRandom(true);
        label.SetText(line);
        const int width = label.GetWidth();

        widest = std::max(widest, width);
        widths += (widths.empty() ? "" : " ") + std::to_string(width);
        (title.empty() ? title : description) += (title.empty() || description.empty() ? "" : "\n") + line;
    }

    const std::size_t drawn = dc::ui::WrapText(title, kPopupWrap).size() + (description.empty() ? 0 : dc::ui::WrapText(description, kPopupWrap).size());

    DrawRngLeave();
    std::fprintf(stderr, "coop: gate popup fit: %zu lines written, %zu drawn (%s), widths %s, widest %d of %d\n", lines.size(), drawn,
                 drawn == lines.size() ? "fits" : "WRAPPED", widths.c_str(), widest, kPopupWrap);
    std::fflush(stderr);
    return drawn == lines.size() ? widest : -1;
}

} // namespace

namespace {

// The popup's title (the question's first line) and description (the other lines).
void PopupParts(const char *question, std::string &title, std::string &description) {
    for (const std::string &line : Lines(question)) {
        if (title.empty()) {
            title = line;
        } else {
            description += (description.empty() ? "" : "\n") + line;
        }
    }
}

// coop-swapbtn (Jep's 11-16 playtest: the swap question's YES plate drew a sheet of a font's glyphs; test hook
// DC_COOP_TEST_PLATE_LOG=1): each drawn frame (the first 10, then one in 60), the key each plate of the popup is drawn from and
// whether that key still holds the plate's own picture (the stamp it was made with) or what lives there now.
void PlateProbe(const std::string &title, const std::string &description) {
    static const bool on = TestEnv("DC_COOP_TEST_PLATE_LOG") != nullptr;
    static int        frames = 0;

    if (!on) {
        return;
    }
    frames++;
    if (frames > 10 && frames % 60 != 0) {
        return;
    }
    for (int b = 0; b < 2; b++) {
        std::uint64_t   made = 0;
        const CTexture *plate = dc::ui::TestButtonPlate(dc::ui::ConfirmModal::ButtonRect(title, description, b),
                                                        dc::ui::ButtonStyle::Thin(dc::ui::ButtonPreset::Terracotta), false, &made);
        const char     *name = b == 0 ? "YES" : "NO";

        if (plate == nullptr) {
            std::fprintf(stderr, "coop-test: popup plate %s: none made (frame %d)\n", name, frames);
            continue;
        }
        const unsigned      key = static_cast<unsigned>(plate->tex0 & 0x3FFF);
        const std::uint64_t now = dc::render::PortKeyStamp(key);

        if (now != 0 && now == made) {
            std::fprintf(stderr, "coop-test: popup plate %s at key %u: its own picture (stamp %llu), frame %d\n", name, key,
                         static_cast<unsigned long long>(made), frames);
            continue;
        }
        std::string held = now == 0 ? "nothing" : "another entry";

        for (int i = 0; i < kTextureCount; i++) {
            const CTexture &t = TexManager.textures[i];

            if (t.name[0] != 0 && static_cast<unsigned>(t.tex0 & 0x3FFF) == key) {
                held = "texture slot " + std::to_string(i) + " \"" + std::string(t.name, strnlen(t.name, sizeof t.name)) + "\"";
                break;
            }
        }
        dc::render::PortTextureOwner owner{};
        unsigned                     w = 0, h = 0;
        const bool                   live = dc::render::PortKeyInfo(key, &owner, &w, &h);

        std::fprintf(stderr,
                     "coop-test: popup plate %s at key %u: NOT its own picture: the key holds %s (owner %d, %ux%u; made with stamp %llu, now %llu), frame %d\n",
                     name, key, held.c_str(), live ? static_cast<int>(owner) : -1, w, h, static_cast<unsigned long long>(made),
                     static_cast<unsigned long long>(now), frames);
    }
}

} // namespace

// coop-swapbtn (test hook DC_COOP_TEST_PLATE_PLANT=<the question, '|' between lines>, on the swap row's asked copy; called by the
// state set's texture rebuild, game/texture.cpp, once it has freed a key the table gives a sheet): that question's YES plate made
// now, at the key the registry hands out next. Returns its key (0: none made).
unsigned PlatePlantMake() {
    std::string question = TestEnv("DC_COOP_TEST_PLATE_PLANT") != nullptr ? TestEnv("DC_COOP_TEST_PLATE_PLANT") : "";
    std::string title, description;

    std::replace(question.begin(), question.end(), '|', '\n');
    PopupParts(question.c_str(), title, description);
    std::uint64_t   made = 0;
    const CTexture *plate = dc::ui::TestButtonPlate(dc::ui::ConfirmModal::ButtonRect(title, description, 0),
                                                    dc::ui::ButtonStyle::Thin(dc::ui::ButtonPreset::Terracotta), true, &made);

    return plate != nullptr ? static_cast<unsigned>(plate->tex0 & 0x3FFF) : 0;
}

// The popup, as the menu's Go Outside question: its first line (or two) the title, the rest the description, YES and NO,
// the hand on cursor (0 Yes, 1 No).
bool DrawYesNoPopup(const char *question, bool changed, int cursor) {
    if (Mutated("gatherbubble")) {
        return false;
    }

    // The title: the first line; the description: the other lines, kept as they are broken (the modal's WrapText breaks at
    // each newline, so a question written in lines of like length reads evenly; Jep, 2026-10-28).
    std::string title, description;

    for (const std::string &line : Lines(question)) {
        if (title.empty()) {
            title = line;
        } else {
            description += (description.empty() ? "" : "\n") + line;
        }
    }

    if (changed) {
        // As move_menu.cpp on its modal's opening: the hand starts on the chosen button, not sliding in from elsewhere.
        constexpr int btn_x = (640 - 100) / 2;
        constexpr int btn_y0 = 164 + 58 + 10;

        g_popup_cursor.SnapTo(static_cast<float>(btn_x - 34), static_cast<float>((cursor == 0 ? btn_y0 : btn_y0 + 26 + 6) + 3));
        std::fprintf(stderr, "coop: gate popup: \"%s\" / \"%s\"\n", title.c_str(), description.c_str());
        PopupFit(question);
    }

    DimForQuestion();
    dc::ui::ConfirmModal::Draw(title, description, dc::localize::LocalizeText("battlemenu.quit.yes", "YES"), dc::localize::LocalizeText("battlemenu.quit.no", "NO"), cursor,
                           g_popup_cursor, 0x80);
    PlateProbe(title, description); // coop-swapbtn
    return true;
}

namespace {

// coop-gatepopup (Jep's 2026-10-28 playtest: the door's question wrapped as "...and / you / go in together. Come now?"): each
// question three short lines broken by hand, well inside the modal's 280-wide wrap (DrawYesNoPopup logs their widths), then
// the bubble twin's two rows. The gathered gate's (together) and the call's (apart).
void GatherText(char *text, int size, int asker, int gather, int to) {
    // pt1111c: an item used at a spot starts a story scene that needs every player (coop_worlds' kGatherItem).
    // coop-readyscreen (Jep 11-13): the ready check's one question, the story door's too (CoopWorldsReadyQuestionText).
    if (gather == 3) {
        CoopWorldsReadyQuestionText(asker, text, size);
        return;
    }
    std::snprintf(text, size, "%s is heading to\nFloor %d's %s.\nGo down together?\nGo together\nNot yet", NetplayShownName((int) asker).c_str(), to + 1,
                  gather == 2 ? "boss fight" : "story scene");
}

// coop-gatevote: a gate's question that needs no gathering (stage 1: go too; 2: the asker asked to leave alone), in the same window.
void VoteText(char *text, int size, int asker, int stage, int where, int to) {
    if (stage == 2) {
        std::snprintf(text, size, "%s isn't coming.\nLeave the floor\nwithout them?\nLeave alone\nStay", NetplayShownName((int) (3 - asker)).c_str());
    } else if (where == 2) {
        std::snprintf(text, size, "%s is heading\nback to town.\nGo with them?\nGo together\nNot now", NetplayShownName((int) asker).c_str());
    } else if (where == 1) {
        std::snprintf(text, size, "%s is heading\ndown to Floor %d.\nGo with them?\nGo together\nNot now", NetplayShownName((int) asker).c_str(), to + 1);
    } else {
        std::snprintf(text, size, "%s is leaving\nthe floor.\nLeave too?\nGo together\nNot now", NetplayShownName((int) asker).c_str());
    }
}

void CallText(char *text, int size, int asker, int to) {
    // coop-firstvisit: a town's story arrival (coop_worlds' kTownStoryTo, -5): the ready check's one question, as a story door's.
    if (to == -5) {
        CoopWorldsReadyQuestionText(asker, text, size);
        return;
    }
    // coop-gateflow: the go-along question of a gate taken to the floor menu (coop_worlds' AskAlong: to -3).
    if (to == -3) {
        std::snprintf(text, size, "%s left the floor\nfor the floor menu.\nGo with them?\nGo now\nStay here", NetplayShownName((int) asker).c_str());
        return;
    }
    // coop-choosing: a floor's back half (AskAlongBack: to -4).
    if (to == -4) {
        std::snprintf(text, size, "%s went through\nthe back half's door.\nGo with them?\nGo now\nStay here", NetplayShownName((int) asker).c_str());
        return;
    }
    std::snprintf(text, size, "%s is heading\ndown to Floor %d.\nJoin them there?\nGo now\nNot now", NetplayShownName((int) asker).c_str(), to + 1);
}

// The first kGatherHead lines of a question's text (the popup's; the rows after are the bubble twin's).
void QuestionHead(char *question, int size, const char *text) {
    std::snprintf(question, size, "%s", text);
    for (int lines = 0, i = 0; question[i] != 0; i++) {
        if (question[i] == '\n' && ++lines == kGatherHead) {
            question[i] = 0;
            break;
        }
    }
}

// coop-forcefloor (Jep 11-14): the call for a floor fenced to the member this copy's player plays: the game UI's option box
// (dc::ui::DialogBox, as the gate's waiting box) with three rows, GO TO FLOOR, SWAP, CANCEL, the hand on the call's cursor.
// Drawn only (CallTick steps its keys); asked only while this player is free (CallFree: no menu, scene or talk under it).
dc::ui::DialogBox g_force_box;
std::string       g_force_text;
int               g_force_drawn = 0;

void DrawForceQuestion(int asker, int to, int chara, int theirs, int cursor) {
    static const char *const kNames[6] = {"Toan", "Xiao", "Goro", "Ruby", "Ungaga", "Osmond"};
    const char *const        floor_member = chara >= 0 && chara < 6 ? kNames[chara] : "?";
    const char *const        offer = theirs >= 0 && theirs < 6 ? kNames[theirs] : "?";
    char                     title[96];
    char                     lines[200];

    std::snprintf(title, sizeof title, "%s is heading to Floor %d", NetplayShownName((int) asker).c_str(), to + 1);
    std::snprintf(lines, sizeof lines, "Only %s can go there,\nand you play %s.\nA swap makes you %s.", floor_member, floor_member, offer);

    const std::string text = std::string(title) + "\n" + lines;

    DrawRngEnter(); // coop-drawrng: the draw stream
    if (text != g_force_text) {
        dc::ui::DialogStyle style;

        style.button_width = 128;
        g_force_box.Configure(title, lines, {dc::ui::DialogOption{"GO TO FLOOR"}, dc::ui::DialogOption{"SWAP"}, dc::ui::DialogOption{"CANCEL"}}, {}, style);
        g_force_box.Open(cursor);
        g_force_text = text;
        g_force_drawn = 0;

        std::string line = text; // (one log line)

        std::replace(line.begin(), line.end(), '\n', '/');
        std::fprintf(stderr, "coop: forcefloor question laid out on this copy (player %d's screen): %s; GO TO FLOOR / SWAP / CANCEL\n", 3 - asker,
                     line.c_str());
        PopupFit(text.c_str());
    }
    g_force_box.SetSelectedIndex(cursor);
    DimForQuestion();
    g_force_box.Draw(0x80);
    DrawRngLeave();
    if (g_force_drawn++ % 60 == 0) {
        static const char *const kRows[3] = {"GO TO FLOOR", "SWAP", "CANCEL"};

        std::fprintf(stderr, "coop: forcefloor question drawn on this copy, hand on %s, %d frames\n", cursor >= 0 && cursor < 3 ? kRows[cursor] : "?",
                     g_force_drawn);
    }
}

void DrawGatherQuestion(int local, bool call) {
    // coop-gatepopup2 (Jep's 2026-10-28 log: apart her popup was laid out again every frame, its hand snapped back each time):
    // apart only the call's path draws (DrawCallQuestion), together only the prompts' (DrawUsePrompts); the other path, also
    // reached apart indoors, leaves the laid-out question alone.
    if (call != CoopWorldsApart()) {
        return;
    }

    int  asked = 0, asker = 0, map = 0, to = 0, gather = 0, cursor = 0, floor = 0, stage = 1, where = 0;
    char text[320];
    int  key = -1;

    // Together: the gate's question; apart: the other player's call (CoopWorldsCallQuestion), on this copy.
    if (!call && EdEventInfo.messages[1] != nullptr && CoopWorldsGatherQuestion(&asked, &asker, &map, &to, &gather, &cursor, &stage, &where)) {
        if (local != 0 && local != asked) {
            return; // the asker's copy waits in its box (DrawWaitingBox)
        }

        key = (((asker * 8 + map) * 128 + to) * 4 + gather) * 8 + (stage - 1) * 3 + where;
        if (gather != 0) {
            GatherText(text, sizeof text, asker, gather, to);
        } else {
            VoteText(text, sizeof text, asker, stage, where, to);
        }
    } else if (call && EdEventInfo.messages[1] != nullptr && CoopWorldsCallQuestion(&asker, &map, &to, &floor, &cursor)) {
        asked = local != 0 ? local : 3 - asker;
        key = 0x100000 + ((asker * 8 + map) * 128 + to) * 128 + floor;
        if (int chara = -1, theirs = -1; CoopWorldsCallForce(&chara, &theirs)) { // coop-forcefloor
            DrawForceQuestion(asker, to, chara, theirs, cursor);
            return;
        }
        CallText(text, sizeof text, asker, to);
    } else if (call == CoopWorldsApart() && CoopWorldsDoorQuestion(&asked, text, sizeof text, &cursor, &key)) {
        // coop-doorgate: a shut door's call, on the asked player's copy (together both copies step it; apart it came here and
        // is drawn with the call's, from the town's draw, indoors too).
        if (local != 0 && local != asked) {
            return;
        }
    }

    if (key < 0) {
        g_gather_key = -1;
        g_gather_drawn = 0;
        g_force_text.clear(); // coop-forcefloor
        return;
    }

    // The popup's question: the text's lines above its two rows.
    char question[320];

    QuestionHead(question, sizeof question, text);

    if (DrawYesNoPopup(question, key != g_gather_key, cursor)) {
        if (key != g_gather_key) {
            g_gather_key = key;
            std::fprintf(stderr, "coop: gate question laid out for player %d's screen (the popup): %s\n", asked, question);
            // coop-callanchor: where the UI mapping places it (x-gate-flow -Wide reads this line): whole, or under a HUD anchor.
            const gfx::UiAnchor anchor = gfx::CurrentUiAnchor();
            const bool          whole = anchor.left_until == -INFINITY && anchor.right_from == INFINITY && anchor.top_until == -INFINITY &&
                               anchor.bottom_from == INFINITY;

            std::fprintf(stderr, "coop: question popup placed %s\n", whole ? "whole in the frame (no anchor)" : "under a HUD anchor (cut on a wide window)");
            std::fflush(stderr);
        }

        if (g_gather_drawn++ % 60 == 0) {
            std::fprintf(stderr, "coop: %s drawn on this copy (player %d's screen) in the game's popup, hand on %s, %d frames\n",
                         key >= 0x100000 ? "call question" : "gate question", local, cursor == 0 ? "Yes" : "No", g_gather_drawn);
        }

        // Test hook (coop-readyscreen, DC_COOP_TEST_READY_SHOT=DIR): a ready check's question, its 30th frame on the asked copy, to
        // DIR/ready-question.png (in-process), once.
        if (static const char *const ready_shot = TestEnv("DC_COOP_TEST_READY_SHOT"); ready_shot != nullptr && coop::ready::IsOpen() && g_gather_drawn == 30) {
            static char path[512];

            std::snprintf(path, sizeof path, "%s/ready-question.png", ready_shot);
            NetplayTestShot(path);
        }

        // Test hook (coop-callanchor, DC_COOP_TEST_CALL_SHOT=DIR): a call question's 20th frame written to DIR/call.png
        // (in-process), once: apart the menu presses' clock (the link's) stands still, so DC_COOP_MENUSHOTS never comes.
        static const char *const call_shot = TestEnv("DC_COOP_TEST_CALL_SHOT");

        if (call_shot != nullptr && key >= 0x100000 && g_gather_drawn == 20) {
            static char path[512];

            std::snprintf(path, sizeof path, "%s/call.png", call_shot);
            NetplayTestShot(path);
        }
        return;
    }

    Window &w = g_gather_window;

    if (key != g_gather_key) {
        DrawRngEnter(); // coop-drawrng: the draw stream
        int                at[4] = {0, 0, 0, 0};

        g_gather_text.Set(0, text);
        w.mes = *EdEventInfo.messages[1];
        w.mes.tex_buff = w.tex;
        w.mes.SetBuff(g_gather_text.Data());
        w.mes.mes_made = -1;
        w.mes.wait = 0;
        w.mes.tail_on = 0;
        w.mes.cursor_row = -1;
        w.mes.cursor_y = 0;
        w.mes.text_rate = 0.0f;
        w.mes.MakeMesWin(0);
        w.mes.auto_pos = local != 0 ? MES_POS_BOTTOM : asked == 1 ? MES_POS_BOTTOM_LEFT : MES_POS_BOTTOM_RIGHT;
        w.mes.AutoSet(at);
        DrawRngLeave();
        g_gather_key = key;
        std::fprintf(stderr, "coop: gate question laid out for player %d's screen: %s\n", asked, text);
        std::fflush(stderr);
    }

    w.mes.cursor_row = kGatherHead + cursor;
    ClsMesSetChoiceRows(&w.mes, kGatherHead);
    DrawWindow(w);
    ClsMesSetChoiceRows(nullptr, 0);

    if (g_gather_drawn++ % 60 == 0) {
        std::fprintf(stderr, "coop: %s drawn on this copy (player %d's screen) in the game's window, hand on row %d (%s), %d frames\n",
                     key >= 0x100000 ? "call question" : "gate question", local, cursor, cursor == 0 ? "yes" : "no", g_gather_drawn);
    }
}

} // namespace

namespace {

// coop-gatepopup (Jep, 2026-10-28: the asker gets "the same box that shows up on the receiving players except it shouldn't say yes
// or no, it should say something like Waiting for players response... and have an indication that you can press the cancel
// button, or circle"): the game UI's own option box (dc::ui::DialogBox, the modal's window, bevel buttons, frame and hand), no
// button (its Circle, shown as the controller's own), on the asker's copy while CoopWorldsWaitingFor. Drawn only: its Circle is stepped with the question
// (coop_worlds: the agreed pad together, this copy's own apart).
dc::ui::DialogBox g_wait_box;
int           g_wait_for = 0; // the player the box names, 0 none

void WaitText(char *text, int size, int asked) {
    // (Jep: "show the button prompt like it does in the inventory": GameText's {circle} token, drawn as the active controller's
    // own button, as the journal's "{circle} Return")
    std::snprintf(text, size, "Waiting for %s\nto answer the invite.\n{circle} Cancel", NetplayShownName((int) asked).c_str());
}

// coop-gatevote (Jep, 2026-11-02): a gate's asker waits in the same box with a ready line for every player and two buttons, LEAVE
// NOW (go without the players who have not said yes; greyed where the floor needs both) and CANCEL (Circle too). Its hand is
// stepped with the vote (coop_worlds AskerPick). Laid out again when a line changes (a no: "Not coming").
std::string g_gate_wait_text;

bool DrawGateWaitingBox() {
    int  on = 0, state = 0, where = 0, to = 0, cursor = 0;
    bool can_leave = false;

    if (!CoopWorldsGateWaiting(&on, &state, &where, &to, &can_leave, &cursor)) {
        if (!g_gate_wait_text.empty()) {
            g_wait_box.Close();
            g_gate_wait_text.clear();
        }
        return false;
    }

    char title[64];
    char lines[160];

    if (where == 3) {
        std::snprintf(title, sizeof title, "Starting a story scene"); // pt1111c: an item scene (coop_worlds' kGatherItem)
    } else if (where == 2) {
        std::snprintf(title, sizeof title, "Heading back to town");
    } else if (to >= 0 && (where == 1 || !can_leave)) {
        std::snprintf(title, sizeof title, "Heading down to Floor %d", to + 1);
    } else {
        std::snprintf(title, sizeof title, "Leaving the floor");
    }
    std::snprintf(lines, sizeof lines, "%s: %s\n%s: %s", NetplayShownName((int) 1).c_str(), on == 1 ? (state == 2 ? "Not coming" : "Waiting...") : "Ready", NetplayShownName((int) 2).c_str(),
                  on == 2 ? (state == 2 ? "Not coming" : "Waiting...") : "Ready");

    const std::string text = std::string(title) + "\n" + lines + (can_leave ? "" : " (needs both)");

    DrawRngEnter(); // coop-drawrng: the draw stream
    if (text != g_gate_wait_text) {
        dc::ui::DialogStyle style;
        dc::ui::DialogOption leave{"LEAVE NOW"};
        dc::ui::DialogOption cancel{"CANCEL"};

        style.button_width = 128;
        leave.enabled = can_leave;
        g_wait_box.Configure(title, lines, {leave, cancel}, {}, style);
        g_wait_box.Open(cursor);
        g_gate_wait_text = text;
        g_wait_for = 0;

        std::string line = text; // (one log line)

        std::replace(line.begin(), line.end(), '\n', '/');
        std::fprintf(stderr, "coop: waiting box laid out on this copy (player %d): %s; LEAVE NOW %s / CANCEL\n", NetplayLocalPlayer(),
                     line.c_str(), can_leave ? "on" : "greyed");
        PopupFit(text.c_str());
    }
    g_wait_box.SetSelectedIndex(cursor);
    DimForQuestion();
    g_wait_box.Draw(0x80);
    DrawRngLeave();

    static int drawn = 0;
    // (coop-warmreset, WS7: put back at each warm-pair job as a fresh copy has it; trace_once_kept twin: kept)
    COOP_TEST_TRACE_RESET(drawn, 0);

    if (drawn++ % 60 == 0) {
        std::fprintf(stderr, "coop: waiting box drawn on this copy, hand on %s, %d frames\n", cursor == 0 ? "LEAVE NOW" : "CANCEL", drawn);
    }
    return true;
}

// coop-readyscreen (Jep 11-13: "the initiating player sees a waiting screen: one row per other player with their portrait and a status
// indicator ... updating live; and two buttons: CANCEL and START NOW"): the ready check's box on its initiator's copy, the same game
// UI box as the gate's (dc::ui::DialogBox, the pause's dim under it, centred: never over the HUD's own pieces, which it dims as the
// pause does). A row per other player: their member's face (the game's own 48 by 48 wheel picture, quickchr.img: CoopDrawHudFace;
// none drawn where the sheet will not load, the member's name stays), "Player N", the member and the status. START NOW and CANCEL,
// the hand stepped with the check (coop_worlds ReadyBoxKeys). Laid out again when a row changes. Rows for every linked player:
// three and more get a row each (design for three).
std::string g_ready_text;

const char *const kMemberNames[6] = {"Toan", "Xiao", "Goro", "Ruby", "Ungaga", "Osmond"};

constexpr int kReadyFace = 18; // a face per row, the modal's line spacing less a little
constexpr int kReadyBoxMax = 440; // the box's widest (the screen is 640; a row up to 392 on one line)

bool DrawReadyBox() {
    namespace ready = coop::ready;

    if (!CoopWorldsReadyBox()) {
        if (!g_ready_text.empty()) {
            g_wait_box.Close();
            g_ready_text.clear();
        }
        return false;
    }

    const ready::Session &session = ready::Get();
    int                   players[coop::kMaxPlayers];
    const int             rows = ready::Rows(players);
    int                   members[coop::kMaxPlayers];
    std::string           lines;

    for (int i = 0; i < rows; i++) {
        // (each player's member from the player table; in town player 2's is the town's partner member, which the table has not)
        int member = coop::ActorChara(players[i] - 1);

        if (member < 0 && mode == 2 && players[i] == 2) { // (2: a town, as DrawEntranceOverlays reads 3 a floor)
            member = CoopTownPartnerMember();
        } else if (member < 0 && players[i] == 1) {
            member = mode == 2 || UserStatus == nullptr ? 0 : UserStatus->cur_chara; // (a town's walker is Toan)
        }

        members[i] = member;
        lines += std::string(i != 0 ? "\n" : "") + NetplayShownName(players[i]) + " (" +
                 (member >= 0 && member < 6 ? kMemberNames[member] : "?") + "): " + ready::StatusText(session.status[players[i]]);
    }

    const std::string title = "Starting a story scene";
    const std::string text = title + "\n" + lines;
    // coop-firstvisit: a story arrival's box has no CANCEL (the arrival cannot be undone), and START NOW only while it is offered
    // (a box waiting on player 1 shows WAITING FOR PLAYER 1 instead, a button that takes nothing).
    // coop-areanotice: the player waited on by the name the rows show, in the buttons' capitals.
    std::string waiting = "WAITING FOR " + NetplayShownName(1);

    std::transform(waiting.begin(), waiting.end(), waiting.begin(), [](unsigned char c) { return (char) std::toupper(c); });

    const std::string buttons = session.start_now && session.cancel ? "START NOW / CANCEL"
                                : session.start_now                 ? "START NOW"
                                : session.cancel                    ? waiting + " / CANCEL"
                                                                    : waiting;

    DrawRngEnter(); // coop-drawrng: the draw stream
    if (text + "\n" + buttons != g_ready_text) {
        dc::ui::DialogStyle  style;
        dc::ui::DialogOption start{session.start_now ? std::string("START NOW") : waiting};
        dc::ui::DialogOption cancel{"CANCEL"};

        style.button_width = 128;
        style.max_width = kReadyBoxMax; // (a row's face, name, member and status: wider than a question's modal)
        if (session.cancel) {
            g_wait_box.Configure(title, lines, {start, cancel}, {}, style);
        } else {
            g_wait_box.Configure(title, lines, {start}, {}, style);
        }
        g_wait_box.Open(session.cancel ? session.cursor : 0);
        g_ready_text = text + "\n" + buttons;
        g_wait_for = 0;

        std::string line = text;

        std::replace(line.begin(), line.end(), '\n', '/');
        std::fprintf(stderr, "coop: ready box laid out on this copy (player %d): %s; %s\n", NetplayLocalPlayer(), line.c_str(), buttons.c_str());

        // Its fit, against its own wrap (the modal wraps at its widest less 48): each row on one line, so each face sits beside its row.
        int widest_row = 0;

        for (const std::string &row : Lines(text)) {
            dc::ui::Label label;

            label.KeepGameRandom(true);
            label.SetText(row);
            widest_row = std::max(widest_row, label.GetWidth());
        }
        std::fprintf(stderr, "coop: ready box fit: widest %d of %d (%s)\n", widest_row, kReadyBoxMax - 48, widest_row <= kReadyBoxMax - 48 ? "fits" : "WRAPPED");
    }
    g_wait_box.SetSelectedIndex(session.cancel ? session.cursor : 0);
    DimForQuestion();
    g_wait_box.Draw(0x80);

    // The faces, each in the box's left margin beside its row (the modal draws its text 22 in, so a face fits in the margin; it trims a
    // row's leading spaces, so none can make room. The modal draws line i of n at box.y + 12 + 20 i, the box ending 10 above its first
    // button and 38 + 20 (n - 1) high; centred, as wide as its widest line and 48, 260 to kReadyBoxMax).
    int widest = 0;

    for (const std::string &row : Lines(text.c_str())) {
        dc::ui::Label label;

        label.KeepGameRandom(true);
        label.SetText(row);
        widest = std::max(widest, label.GetWidth());
    }

    const int  n = 1 + rows;
    const auto button = g_wait_box.GetButtonRect(0);
    const int  box_w = std::clamp(widest + 48, 260, kReadyBoxMax);
    const int  box_x = (640 - box_w) / 2;
    int        faces = 0;

    for (int i = 0; i < rows; i++) {
        const int line_y = (int) button.y - 36 - (n - 1 - (i + 1)) * 20;

        faces += CoopDrawHudFace(members[i], box_x + 3, line_y - 1, kReadyFace) ? 1 : 0;
    }
    DrawRngLeave();

    static int drawn = 0;
    // (coop-warmreset, WS7: put back at each warm-pair job as a fresh copy has it; trace_once_kept twin: kept)
    COOP_TEST_TRACE_RESET(drawn, 0);

    if (drawn++ % 60 == 0) {
        std::fprintf(stderr, "coop: ready box drawn on this copy, %d rows, %d faces drawn, hand on %s, %d frames\n", rows, faces,
                     session.cursor == ready::kButtonStartNow ? "START NOW" : "CANCEL", drawn);
    }
    // Test hook (DC_COOP_TEST_READY_SHOT=DIR): the box's 30th frame written to DIR/ready-box.png (in-process), once; and as the
    // waiting box's (DC_COOP_TEST_WAIT_SHOT=DIR: DIR/waiting.png, x-door-gate refuse and cancel).
    if (static const char *const shot = TestEnv("DC_COOP_TEST_READY_SHOT"); shot != nullptr && drawn == 30) {
        static char path[512];

        std::snprintf(path, sizeof path, "%s/ready-box.png", shot);
        NetplayTestShot(path);
    }
    if (static const char *const wait_shot = TestEnv("DC_COOP_TEST_WAIT_SHOT"); wait_shot != nullptr && drawn == 30) {
        static char path[512];

        std::snprintf(path, sizeof path, "%s/waiting.png", wait_shot);
        NetplayTestShot(path);
    }
    return true;
}

bool DrawWaitingBox() {
    int asked = 0;

    if (DrawReadyBox()) {
        return true;
    }
    if (DrawGateWaitingBox()) {
        return true;
    }

    if (!CoopWorldsWaitingFor(&asked)) {
        if (g_wait_for != 0) {
            g_wait_box.Close();
            g_wait_for = 0;
        }
        return false;
    }

    DrawRngEnter(); // coop-drawrng: the draw stream

    if (asked != g_wait_for) {
        char text[160];

        WaitText(text, sizeof text, asked);

        const std::vector<std::string> lines = Lines(text);
        std::string                    description;

        for (std::size_t i = 1; i < lines.size(); i++) {
            description += (i > 1 ? "\n" : "") + lines[i];
        }
        // (no button: "you dont need to have the cancel selectable written on the screen since circle already does it", Jep)
        g_wait_box.Configure(lines.empty() ? "" : lines[0], description, {});
        g_wait_box.Open(0);
        g_wait_for = asked;
        std::fprintf(stderr, "coop: waiting box laid out on this copy (player %d): %s\n", NetplayHasPlayer() ? NetplayLocalPlayer() : 0, text);
        PopupFit(text);
    }
    DimForQuestion();
    g_wait_box.Draw(0x80);
    DrawRngLeave();

    // Test hook (DC_COOP_TEST_WAIT_SHOT=DIR): its 30th frame written to DIR/waiting.png (in-process), once.
    static const char *const shot_dir = TestEnv("DC_COOP_TEST_WAIT_SHOT");
    static int               drawn = 0;

    if (shot_dir != nullptr && ++drawn == 30) {
        static char path[512];

        std::snprintf(path, sizeof path, "%s/waiting.png", shot_dir);
        NetplayTestShot(path);
    }
    return true;
}

// coop-gatepopup test hook (DC_COOP_TEST_POPUP_TOUR=DIR[:FROM]): from this copy's town frame FROM (900 unset) on, every question
// the popup asks, 40 frames each, its fit logged and one frame of each written to DIR/popup-NN.png (in-process): every door of
// the table, the cave door, each town's Georama scene, the gathered gate's story and boss and the call (one- and two-digit floors).
bool PopupTour() {
    static const char *const spec = TestEnv("DC_COOP_TEST_POPUP_TOUR");
    static std::vector<std::string> questions;
    static int                      frame = 0, widest = 0, wrapped = 0;
    static bool                     done = false;

    if (spec == nullptr || done) {
        return false;
    }

    static std::string dir = spec;
    static int         from = 900;

    if (questions.empty()) {
        if (const std::size_t colon = dir.rfind(':'); colon != std::string::npos && colon > 1) {
            from = std::atoi(dir.c_str() + colon + 1);
            dir.resize(colon);
        }

        char text[320], question[320];
        auto add = [&]() {
            QuestionHead(question, sizeof question, text);
            questions.emplace_back(question);
        };

        for (int row = 0; row < CoopWorldsDoorRows(); row++) {
            CoopWorldsDoorQuestionText(1, row, false, text, sizeof text);
            add();
        }
        CoopWorldsDoorQuestionText(2, -1, true, text, sizeof text);
        add();
        for (int town = 0; town < 5; town++) {
            CoopWorldsDoorQuestionText(2, -2 - town, false, text, sizeof text);
            add();
        }
        for (int to : {4, 14}) {
            GatherText(text, sizeof text, 1, 1, to);
            add();
            GatherText(text, sizeof text, 2, 2, to);
            add();
            CallText(text, sizeof text, 1, to);
            add();
        }
    }

    const int at = frame++ - from;

    if (at < 0) {
        return false;
    }

    const int n = at / 40;

    if (n >= (int) questions.size()) {
        done = true;
        std::fprintf(stderr, "coop: popup tour done: %zu questions, widest %d of %d, %d wrapped\n", questions.size(), widest, kPopupWrap, wrapped);
        std::fflush(stderr);
        return false;
    }

    const char *question = questions[n].c_str();

    if (at % 40 == 0) {
        std::fprintf(stderr, "coop: popup tour %02d: %s\n", n, question);
        const int fit = PopupFit(question);

        widest = std::max(widest, fit);
        wrapped += fit < 0 ? 1 : 0;
    } else if (at % 40 == 20) {
        static char path[512];

        std::snprintf(path, sizeof path, "%s/popup-%02d.png", dir.c_str(), n);
        NetplayTestShot(path);
    }

    DrawYesNoPopup(question, at % 40 == 0, n % 2);
    return true;
}

} // namespace

// coop-townwheel (Jep, 2026-11-02: the character swap prompt "drawn with the in game yes/no menu", no white text): the swap
// question (and the revive powder's) in the game's Yes/No popup on the asked player's copy, the hand where their pad put it
// (coop_prompt StepPrompt); the asker's copy waits in the gate's kind of box, its Circle the cancel. Neither over that copy's
// own menu (its question waits for it, StepPrompt). Couch (one screen): the question alone.
// DC_COOP_MUTATE=swap_caption (twin): the old white caption line on the asked player's copy.
namespace {

dc::ui::DialogBox g_swap_box;
std::string   g_swap_text; // the popup's question as laid out, "" none
int           g_swap_waiting = 0; // the player the box names, 0 none
int           g_swap_drawn = 0;

void SwapQuestionText(const PromptView &v, char *text, int size) {
    static const char *const kNames[6] = {"Toan", "Xiao", "Goro", "Ruby", "Ungaga", "Osmond"};

    if (v.revive) {
        std::snprintf(text, size, "Use a Revive Powder?\nYou carry %d.", v.carried);
    } else if (v.spot >= 0) {
        std::snprintf(text, size, "This is %s's spot.\nSwap to %s?", kNames[v.spot], kNames[v.spot]);
    } else {
        std::snprintf(text, size, "%s wants %s.\nSwap %s for %s?", NetplayShownName((int) v.asker).c_str(), kNames[v.wanted], kNames[v.wanted], kNames[v.own]);
    }
}

void CloseSwapBox() {
    if (g_swap_waiting != 0) {
        g_swap_box.Close();
        g_swap_waiting = 0;
    }
}

} // namespace

// coop-swapcombo part 4 (DC_COOP_TEST_SWAP_SHOT=<path prefix>, tests): the popup's and the waiting box's 10th drawn frame to
// <prefix>-question.png and <prefix>-box.png, once each, to compare the trade's look together and apart.
void SwapShot(int drawn, const char *what) {
    static const char *prefix = TestEnv("DC_COOP_TEST_SWAP_SHOT");
    static bool        taken[2] = {};
    const int          k = what[0] == 'q' ? 0 : 1;

    if (prefix == nullptr || taken[k] || drawn != 10) {
        return;
    }
    taken[k] = true;
    static std::string path[2];

    path[k] = std::string(prefix) + "-" + what + ".png";
    NetplayTestShotRequest(path[k].c_str());
    std::fprintf(stderr, "coop-test: swap %s frame asked for %s\n", what, path[k].c_str());
}

// coop-revive2: a reviver's Cross with no Revive Powder in its bag: "You have no Revive Powder." in the game's box on that player's
// screen alone (couch: the one screen) for a few seconds, gone by itself. Draw only (coop_prompt's frames, presentation).
namespace {
dc::ui::DialogBox g_revive_none_box;
bool              g_revive_none_open = false;
int               g_revive_none_drawn = 0;

void DrawReviveNone() {
    const int local = CoopWorldsApart() ? CoopWorldsSelf() : NetplayLocalPlayer();
    bool      show = false;

    for (int p = 1; p <= kMaxPlayers; p++) {
        show = show || (ReviveNoneFrames(p) > 0 && (local == 0 || local == p));
    }
    ReviveNoneTick();
    if (!show) {
        if (g_revive_none_open) {
            g_revive_none_box.Close();
            g_revive_none_open = false;
        }
        return;
    }
    if (!g_revive_none_open) {
        g_revive_none_box.Configure("You have no Revive Powder.", "", {});
        g_revive_none_box.Open(0);
        g_revive_none_open = true;
        g_revive_none_drawn = 0;
        std::fprintf(stderr, "coop-revive: no-powder box laid out on P%d's screen\n", local);
    }
    DrawRngEnter(); // coop-drawrng: the draw stream
    g_revive_none_box.Draw(0x80);
    DrawRngLeave();
    if (g_revive_none_drawn++ % 60 == 0) {
        std::fprintf(stderr, "coop-revive: no-powder box drawn on P%d's screen, %d frames\n", local, g_revive_none_drawn);
    }
}
} // namespace

void DrawSwapQuestion() {
    PromptView v;

    DrawReviveNone(); // coop-revive2
    // (coop-swapcombo: apart each copy is its own player's, the trade across zones' popup or box)
    const int  local = CoopWorldsApart() ? CoopWorldsSelf() : NetplayLocalPlayer();

    if (!PromptQuestion(&v)) {
        CloseSwapBox();
        g_swap_text.clear();
        g_swap_drawn = 0;
        return;
    }

    char text[160];

    SwapQuestionText(v, text, sizeof text);

    // The asked player's screen: the question (couch: the one screen).
    if (local == 0 || local == v.asked) {
        CloseSwapBox();
        if (FullMenuOpenFor(v.asked) && !Mutated("swap_over_menu")) {
            return;
        }
        if (Mutated("swap_caption")) {
            dc::text::caption::LineStyle style;

            style.status_font = true;
            style.edge_width = 1.5f;
            dc::text::caption::DrawLine(std::string(text) + "  Cross yes / Circle no", 320.0f, 330.0f, 10.0f, 1.0f, 560.0f, style, 1.0f, 1.0f, nullptr);
            if (g_swap_drawn++ % 60 == 0) {
                std::fprintf(stderr, "coop: swap question drawn on this copy (player %d's screen) as a caption line, %d frames\n", local, g_swap_drawn);
            }
            return;
        }

        const bool changed = text != g_swap_text;

        const bool drawn = DrawYesNoPopup(text, changed, v.cursor); // (as the gate's question draws it)
        if (changed) {
            g_swap_text = text;
            std::string line = text;

            std::replace(line.begin(), line.end(), '\n', '/');
            std::fprintf(stderr, "coop: swap question laid out for player %d's screen (the popup): %s\n", v.asked, line.c_str());
        }
        SwapShot(g_swap_drawn, "question"); // coop-swapcombo part 4
        if (g_swap_drawn++ % 60 == 0) {
            std::fprintf(stderr, "coop: swap question drawn on this copy (player %d's screen) %s, hand on %s, %d frames\n", local,
                         drawn ? "in the game's popup" : "NOT DRAWN (no popup pictures)", v.cursor == 0 ? "Yes" : "No", g_swap_drawn);
        }
        return;
    }

    // The asker's screen: its box (the revive's asker lies down and is asked nothing, so it has none).
    if (local != v.asker || v.revive || (FullMenuOpenFor(v.asker) && !Mutated("swap_over_menu"))) {
        CloseSwapBox();
        return;
    }

    DrawRngEnter(); // coop-drawrng: the draw stream
    if (g_swap_waiting != v.asked) {
        char wait[160];

        std::snprintf(wait, sizeof wait, "Waiting for %s\nto answer the swap.\n{circle} Cancel", NetplayShownName((int) v.asked).c_str());

        const std::vector<std::string> lines = Lines(wait);
        std::string                    description;

        for (std::size_t i = 1; i < lines.size(); i++) {
            description += (i > 1 ? "\n" : "") + lines[i];
        }
        g_swap_box.Configure(lines.empty() ? "" : lines[0], description, {});
        g_swap_box.Open(0);
        g_swap_waiting = v.asked;
        std::fprintf(stderr, "coop: swap waiting box laid out on this copy (player %d): %s\n", local, wait);
    }
    DimForQuestion();
    g_swap_box.Draw(0x80);
    DrawRngLeave();
    SwapShot(g_swap_drawn, "box"); // coop-swapcombo part 4
    if (g_swap_drawn++ % 60 == 0) {
        std::fprintf(stderr, "coop: swap waiting box drawn on this copy (player %d), %d frames\n", local, g_swap_drawn);
    }
}

// coop-floorgate: the other player's call, apart (alone on a floor or in a town, where no co-op HUD draws the prompts).
void DrawCallQuestion() {
    if (CoopWorldsApart() && PopupTour()) { // (together DrawUsePrompts steps it)
        return;
    }
    if (CoopWorldsApart()) {
        // coop-callanchor (Jep's 2026-11-04 pack 8 playtest: P2's "Player 1 left the floor for the floor menu. Go with them?" came up
        // cut down the middle): alone on a floor this is reached from CoopDrawHud, inside the dungeon HUD's status anchor, so on a
        // wide window each piece of the popup whose centre lies left of 0x124 went to the window's left edge and the rest stayed
        // centred (coop-usebands2's cut, which CoopDrawWheel's scope never covered here). The popup and the asker's box are placed
        // in the frame whole. DC_COOP_MUTATE=callanchor (twin): under the HUD's anchor, as before.
        std::optional<gfx::UiAnchorScope> whole;

        if (!Mutated("callanchor")) {
            whole.emplace(gfx::UiAnchor{});
        }

        DrawWaitingBox(); // coop-gatepopup: the asker's box (apart)
        DrawGatherQuestion(NetplayLocalPlayer(), true);
        DrawSwapQuestion(); // coop-swapcombo part 2: a trade across zones, as together's
    }
}

// coop-gatepopup2: at a dungeon's entrance floor menu the floor's HUD (CoopDrawHud) is not drawn, and with it neither the call's
// boxes nor the notice: drawn here over the menu (coop_menu's MenuDrawPromptCover, late in the floor's frame), apart.
void DrawEntranceOverlays() {
    if (mode != 3 || BtGameModeFlag != BT_GAME_MODE_ENTRANCE_MENU || !CoopWorldsApart() || Mutated("entrance_overlays_off")) {
        return;
    }
    DrawCallQuestion();
    NetplayDrawNotice();

    static int said = 0;

    if (said++ % 600 == 0) {
        std::fprintf(stderr, "coop: the call's boxes and the notice drawn over the floor menu, %d frames\n", said);
        std::fflush(stderr);
    }
}

// The user's prompt, on that player's copy only (couch: both; his on the left, hers on the right), and its last line for a
// while after. DC_COOP_MUTATE=promptboth draws them on both copies (a twin for the screens' check).
void DrawUsePrompts() {
    const int local = NetplayLocalPlayer();

    if (PopupTour()) { // coop-gatepopup: a test's tour of the popup's questions
        return;
    }
    DrawWaitingBox(); // coop-gatepopup: the asker's box (together)
    DrawGatherQuestion(local, false); // coop-floorgate: the gate's question (together)

    for (int player = 1; player <= 2; player++) {
        Prompt &p = g_prompts[player - 1];

        if (p.kind == Kind::None && (p.note_ticks == 0 || p.note_mes < 0)) {
            g_windows[player - 1].made = -1;
        }

        if ((p.kind == Kind::None && p.note_ticks == 0) || (local != 0 && local != player && !Mutated("promptboth")) ||
            (p.kind == Kind::Gate && p.step == Step::Waiting)) {
            continue;
        }

        if ((p.kind == Kind::Chest || p.kind == Kind::Piero || p.kind == Kind::Message) && p.mes >= 0 && !Mutated("promptcaption")) {
            PutWindow(g_windows[player - 1], p, player, local);
            DrawWindow(g_windows[player - 1]);

            if (g_drawn[player - 1]++ % 60 == 0) {
                std::fprintf(stderr, "coop: %s prompt drawn on this copy (player %d's screen), message %d in the game's window, %d frames\n",
                             player == 1 ? "player 1's" : "partner's", local, p.mes, g_drawn[player - 1]);
            }

            continue;
        }

        dc::text::caption::LineStyle style;

        style.status_font = true;
        style.edge_width = 1.5f;

        static const spRGBA kBack = {0x08, 0x10, 0x48, 0x70};
        const int           left = player == 1 ? 30 : 350;
        constexpr int       kTop = 300;
        constexpr int       kWidth = 260;
        const float         centre = left + kWidth / 2.0f;

        if (p.kind == Kind::None && p.note_mes >= 0) { // coop-playfix: a door's message, in the game's window
            Prompt shown = p;

            shown.mes = p.note_mes;
            shown.step = Step::Read;
            PutWindow(g_windows[player - 1], shown, player, local);
            DrawWindow(g_windows[player - 1]);
            if (g_drawn[player - 1]++ % 60 == 0) {
                std::fprintf(stderr, "coop: %s note drawn on this copy (player %d's screen), message %d in the game's window, %d frames\n",
                             player == 1 ? "player 1's" : "partner's", local, p.note_mes, g_drawn[player - 1]);
            }
            continue;
        }

        if (p.kind == Kind::None && DrawTextInGameWindow(player - 1, player, local, p.note, 1, -1)) {
            continue;
        }
        if (p.kind != Kind::None) {
            std::string text;

            for (int i = 0; i < p.head.size(); i++) {
                text += std::string(text.empty() ? "" : "\n") + p.head[i];
            }
            for (int i = 0; i < p.rows.size(); i++) {
                text += std::string(text.empty() ? "" : "\n") + p.rows[i];
            }
            if (DrawTextInGameWindow(player - 1, player, local, text, (int) p.head.size(), p.step == Step::Read ? -1 : ShownCursor(player, p))) {
                continue;
            }
        }

        if (p.kind == Kind::None) {
            set2DSpriteC4(Vif1Packet, CRect_i_(left, kTop - 4, kWidth, 20), (spRGBA *) &kBack, (spRGBA *) &kBack, (spRGBA *) &kBack, (spRGBA *) &kBack);
            dc::text::caption::DrawLine(p.note, centre, (float) kTop, 8.0f, 1.0f, kWidth - 10.0f, style, 1.0f, 1.0f, nullptr);
            // coop-menucross: the note's draw, once a second (the menucross test's line)
            if (CoopDebug() && g_drawn[player - 1]++ % 60 == 0) {
                std::fprintf(stderr, "coop: %s note drawn on this copy (player %d's screen): %s\n", player == 1 ? "player 1's" : "partner's", local, p.note);
            }
            continue;
        }

        const int lines = (int) (p.head.size() + p.rows.size());

        set2DSpriteC4(Vif1Packet, CRect_i_(left, kTop - 4, kWidth, 24 + 11 * lines), (spRGBA *) &kBack, (spRGBA *) &kBack, (spRGBA *) &kBack,
                      (spRGBA *) &kBack);

        float y = (float) kTop;

        for (int i = 0; i < p.head.size(); i++) {
            dc::text::caption::DrawLine(p.head[i], centre, y, 8.0f, 1.0f, kWidth - 10.0f, style, 1.0f, 1.0f, nullptr);
            y += 11.0f;
        }

        for (int row = 0; row < (int) p.rows.size(); row++) {
            dc::text::caption::LineStyle row_style = style;
            const bool         current = row == ShownCursor(player, p);

            if (current) {
                row_style.fill[2] = 120;
            }

            dc::text::caption::DrawLine(std::string(current ? "> " : "") + p.rows[row], centre, y, 8.0f, 1.0f, kWidth - 10.0f, row_style, current ? 1.0f : 0.75f, 1.0f,
                              nullptr);
            y += 11.0f;
        }

        dc::text::caption::DrawLine(p.step == Step::Read ? "X next" : "X choose   O back", centre, y + 2.0f, 7.0f, 1.0f, kWidth - 10.0f, style, 0.8f, 1.0f,
                          nullptr);

        if (g_drawn[player - 1]++ % 60 == 0) {
            std::fprintf(stderr, "coop: %s prompt drawn on this copy (player %d's screen), message %d, %d frames\n",
                         player == 1 ? "player 1's" : "partner's", local, p.mes, g_drawn[player - 1]);
        }
    }
}

} // namespace coop

// The gate's escape window (_OPEN_ESCAPE_WINDOW, ps2/src/btsysscript.cpp): answered already when the players said yes in
// their own prompts (coop-p2prompt), so the script reads the answer and no window opens.
namespace dc::game { // repin: was PORT_OVERRIDE; a recipe removes the game's own definition
int _OPEN_ESCAPE_WINDOW(RS_STACKDATA *stack, int argument_count) {
    if (stack->type != RS_PTR) {
        return 0;
    }

    int answer = 0;

    if (coop::EscapeWindowAnswer(&answer)) {
        stack->p->type = RS_INT;
        stack->p->i = answer;
        return 1;
    }

    BtEventInfo.escape_result = stack->p;
    BtBattleMusic_Stop();
    BtEventInfo.request = BT_REQUEST_ESCAPE_WINDOW;
    return 1;
}
} // namespace dc::game

// coop-gatepopup2: the dungeon's floor menu draws (ps2/src/menu_dungeon.cpp's DunEnterMenuLoop, after DunEnterDraw): a call's boxes and
// the notice over it (coop::DrawEntranceOverlays).
void CoopEntranceDraw() {
    coop::DrawEntranceOverlays();
}
