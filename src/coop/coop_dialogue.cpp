#include "coop/game_ns.hpp"
#include "coop/coop_dialogue.hpp"

#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "clsmes.hpp"
#include "dun/gameloop.hpp"
#include "editloop.hpp"
#include "text/gametext.hpp"
#include "main.hpp"
#include "userstatus.hpp"

#include "coop/coop.hpp"
#include "coop/coop_core.hpp"
#include "coop/coop_town.hpp"
#include "coop/coop_worlds.hpp"

namespace dc::game {
extern ClsMes DngMes1; // dun/gameloop: a floor's talk file
extern s32    mode;
} // namespace dc::game

namespace {

const char *const kNames[6] = {"Toan", "Xiao", "Goro", "Ruby", "Ungaga", "Osmond"};

// A member's name in a line: the code ClsMes draws as member N's name from the save (SetAndGetNameRegistTbl), N from
// 0 (Toan, the one the villagers' lines call the player by) to 5.
constexpr short kNameCode = -0x506;

// A line with her name in: the file's text it was made from and her member, so a change of either makes it again.
struct Swapped {
    const short       *from = nullptr;
    int                member = -1;
    bool               changed = false;
    std::vector<short> text;
};

std::map<std::pair<const ClsMes *, int>, Swapped> g_lines;

// Player 2's member: in a town or building the one pad 2 plays there; on a floor her copy's player while apart (she
// became it), else her partner.
int SpeakerMember() {
    if (mode == GAME_MODE_EDIT) {
        return coop::TownPlayerMember();
    }
    if (CoopWorldsApart()) {
        return UserStatus != nullptr ? UserStatus->cur_chara : -1;
    }
    return coop::g_partner.loaded_chara;
}

// text with Toan's name code as member's; whether it had any.
bool Swap(const short *text, int member, std::vector<short> &out) {
    bool changed = false;

    out.clear();
    for (const short *at = text; *at != MES_CODE_END; at++) {
        if (*at == kNameCode) {
            // DC_COOP_MUTATE=no_dlgname (the twin): the player's name stays Toan's, every line still logged as hers.
            out.push_back(static_cast<short>(coop::Mutated("no_dlgname") ? kNameCode : kNameCode + member));
            changed = true;
        } else {
            out.push_back(*at);
        }
    }
    out.push_back(MES_CODE_END);

    return changed;
}

// Test hook (DC_COOP_DLGNAME_SCAN=1, with DC_COOP_DEBUG): the first time her dialogue reads a talk file, every line of
// it that calls the player by name is logged as her copy would show it (the lines the test's room shows may not).
void ScanFile(const short *file, int member) {
    static const short *scanned[2] = {};
    std::vector<short>  out;

    if (file == scanned[0] || file == scanned[1] || !CoopDebug() || !TestEnvOn("DC_COOP_DLGNAME_SCAN")) {
        return;
    }
    scanned[scanned[0] == nullptr ? 0 : 1] = file;

    const int    count = file[0];
    const short *body = &file[1];
    int          found = 0;

    for (int i = 0; i < count; i++) {
        if (Swap(&body[count] + body[i * 2 + 2], member, out)) {
            found++;
            std::fprintf(stderr, "coop-dlgname: scan: line %d said by player 2 (%s): %s\n", body[i * 2 + 1], kNames[member],
                         dc::text::GameTextDecode(out.data()).c_str());
        }
    }
    std::fprintf(stderr, "coop-dlgname: scan: %d of %d lines name the player\n", found, count);
}

} // namespace

short *CoopDialogueLine(const ClsMes *mes, int line, short *text) {
    if (text == nullptr || mes->buff == nullptr || (mes->buff != EditMes1.buff && mes->buff != DngMes1.buff)) {
        return text;
    }

    // Test hook (DC_COOP_TEST_DLG_NAME=1; coop-pt1109b, the villagers' lines that call the player by name): every line of the town's or
    // room's talk file starts with the player's name code (the line otherwise as it is, so a talk runs as long), so a talk shows whose name
    // it uses; each change is logged with whose talk it is.
    static const bool force = TestEnvOn("DC_COOP_TEST_DLG_NAME");
    static std::map<int, std::vector<short>> forced;
    static std::map<int, std::string>        shown;
    const short                             *forced_text = nullptr;

    if (force && mes->buff == EditMes1.buff && mode == GAME_MODE_EDIT) {
        std::vector<short> &made = forced[line];

        if (made.empty() || made.size() != 1 + [&] { std::size_t n = 0; while (text[n] != MES_CODE_END) n++; return n + 1; }()) {
            made.assign(1, kNameCode);
            for (const short *at = text; *at != MES_CODE_END; at++) {
                made.push_back(*at);
            }
            made.push_back(MES_CODE_END);
        }
        text = made.data();
        forced_text = text;
    }
    const bool second_talks = CoopTownHerTalkOn();
    const auto logged = [&](const short *result) {
        if (!force || forced_text == nullptr) {
            return result;
        }
        const std::string now = dc::text::GameTextDecode(result).substr(0, 7) + (second_talks ? " (her talk)" : " (not hers)");
        if (shown[line] != now) {
            shown[line] = now;
            std::fprintf(stderr, "coop-dlgname: test: line %d reads %s%s", line, now.c_str(), "\n");
        }
        return result;
    };

    if (!CoopWorldsPartnerSpeaks()) {
        return const_cast<short *>(logged(text));
    }

    const int member = SpeakerMember();
    if (member <= 0 || member >= 6) {
        return const_cast<short *>(logged(text));
    }
    ScanFile(mes->buff, member);

    Swapped &swapped = g_lines[{mes, line}];
    if (swapped.from != text || swapped.member != member) {
        swapped.from = text;
        swapped.member = member;
        swapped.changed = Swap(text, member, swapped.text);

        if (CoopDebug()) {
            std::fprintf(stderr, "coop-dlgname: line %d said by player 2 (%s)%s: %s\n", line, kNames[member],
                         swapped.changed ? ", her name for the player's" : "", dc::text::GameTextDecode(swapped.text.data()).c_str());
        }
    }

    return const_cast<short *>(logged(swapped.changed ? swapped.text.data() : text));
}
