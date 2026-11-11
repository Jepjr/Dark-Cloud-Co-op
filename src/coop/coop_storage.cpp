// coop-small2 (Jep 11-09, storage option C): the house's storage (the check-in counter's board, CStockItem: 60 items, 30 weapons, 30
// attachments) is the party's one board, and any number of players may use it at once, together or apart.
//
// Retail's counter (shop.cpp ChargeShopKey / ChargeSelectKey / ShopIconMove) changes the storage at five presses: Cross on a storage
// cell (lift; drop; or swap the held record with the cell's), Triangle-take (the cell's record straight into the bag), Triangle-put
// (a bag record into the first free cell, landing as its icon arrives), Circle (a lifted record back to its cell) and Square (sort).
// With C every one of them is a real step of the party's board, never the cursor's working writes:
//   - a PICK-UP (a lift, a take, the cell's half of a swap) claims the cell at the press: it lands, the cell's record leaves the board on
//     every copy and reaches the presser's hand (or bag), or, the cell no longer holding what the presser saw (another's claim landed
//     first), it is refused: the refuse sound, nothing in the hand. Nothing ever shows in a hand and is taken back.
//   - a PUT-DOWN goes at once as one finished move: the record into the cell asked for, or (taken by then) the page's first free cell.
//   - a sort is one move too (retail's Seiton on the board as it then is).
// Together (linked, local menus) the claim and the move are lockstep commands, kCmdStorageClaim and kCmdStorageMove (a weapon's or an
// attachment's record goes first as staged words, then its commit), applied on every copy on one tick: the first to act is the same
// everywhere. Apart the link is down, so the host owns the board: its own presses apply at once (its live slot records carry them),
// another player's go to the host as an ask and land when the host's answer comes back (a pick-up waits that round trip; a put-down
// leaves the hand at once). Nobody else waits on anyone (merge-no-wait). Players by number, any count (the host is player 1).
// DC_COOP_MUTATE=storage_retail (twin): retail's exchanges on the presser's copy, as before (a lift lands at once wherever it was seen).

#include "coop/coop_storage.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <vector>

#include "itemdata.hpp"
#include "savedata.hpp"
#include "stockitem.hpp"

#include "coop/coop.hpp"
#include "coop/coop_actor.hpp" // kMaxPlayers
#include "coop/coop_core.hpp"
#include "coop/coop_fullmenu.hpp"
#include "coop/coop_townmenu.hpp"
#include "coop/coop_worlds.hpp" // (CoopWorldsBagDump: the tests' storage lines)
#include "coop/netplay.hpp"
#include "coop/netplay_cmd.hpp"
#include "coop/netplay_socket.hpp"

using namespace coop;

void CoopWorldsSlotRebase(int region, int index); // coop_worlds.cpp: this copy's live-sync baseline of a slot set to what it holds now

namespace {

constexpr int kPageItem = 0, kPageWeapon = 1, kPageAttach = 2; // BOARD_PAGE_* (menu_draw.hpp); also the live sync's storage regions
constexpr int kMaxRecord = sizeof(WEAPON_HAVE);
static_assert(sizeof(WEAPON_HAVE) % 4 == 0 && sizeof(ATTACH_LIST) % 4 == 0);

// The commands' a: the page in bits 0-1; kCmdStorageMove: what in bits 2-3 (0 a put's commit, 1 a staged word, 2 a sort), bit 4 the
// commit takes the staged words (else c is an item's {item, uses}); kCmdStorageClaim: bit 2 the take into the bag, bit 3 a swap (the
// staged words go into the cell). b: the cell (a staged word: its number). c: the value, or the item the claimer saw in the cell.
constexpr int kMoveCommit = 0, kMoveWord = 1, kMoveSort = 2;

int PageCount(int page) {
    return page == kPageItem ? 60 : 30;
}

int RecordSize(int page) {
    return page == kPageItem ? 4 : page == kPageWeapon ? (int) sizeof(WEAPON_HAVE) : (int) sizeof(ATTACH_LIST);
}

const char *PageName(int page) {
    return page == kPageItem ? "item" : page == kPageWeapon ? "weapon" : "attachment";
}

CStockItem *Board() {
    return SaveData != nullptr ? SaveData->GetStockItem() : nullptr;
}

int CellItem(const CStockItem *board, int page, int slot) {
    return page == kPageItem ? board->dungeon_items[slot] : page == kPageWeapon ? board->weapons[slot].item_no : board->attachments[slot].item_no;
}

bool Empty(int item) {
    return item < ITEM_ATTACH_START;
}

void ReadCell(const CStockItem *board, int page, int slot, void *out) {
    if (page == kPageItem) {
        const std::int16_t pair[2] = {board->dungeon_items[slot], board->dungeon_item_vols[slot]};
        std::memcpy(out, pair, 4);
    } else if (page == kPageWeapon) {
        std::memcpy(out, &board->weapons[slot], sizeof(WEAPON_HAVE));
    } else {
        std::memcpy(out, &board->attachments[slot], sizeof(ATTACH_LIST));
    }
}

void WriteCell(CStockItem *board, int page, int slot, const void *in) {
    if (page == kPageItem) {
        std::int16_t pair[2];
        std::memcpy(pair, in, 4);
        board->dungeon_items[slot] = pair[0];
        board->dungeon_item_vols[slot] = pair[1];
    } else if (page == kPageWeapon) {
        std::memcpy(&board->weapons[slot], in, sizeof(WEAPON_HAVE));
    } else {
        std::memcpy(&board->attachments[slot], in, sizeof(ATTACH_LIST));
    }
}

void ClearCell(CStockItem *board, int page, int slot) {
    std::uint8_t none[kMaxRecord] = {};

    WriteCell(board, page, slot, none); // (retail's take leaves 0 there too)
}

int RecordItem(int page, const void *data) {
    std::int16_t item;

    std::memcpy(&item, data, 2); // (every record starts with its item_no)
    return item;
}

// What lands for this copy's own player: the counter's next step takes it.
struct Event {
    bool         granted = false;
    int          page = 0, slot = 0, purpose = 0;
    std::uint8_t data[kMaxRecord] = {};
};
std::deque<Event> g_events;
int               g_waiting = 0; // this copy's player's claims out, not landed

std::array<std::vector<std::uint32_t>, kMaxPlayers + 1> g_stage; // each player's staged words (together, every copy alike)

void Said(const char *line) {
    std::fputs(line, stderr);
    std::fflush(stderr);
}

int Me();

void Landed(int player, bool granted, int page, int slot, int purpose, const void *data, bool claim = true) {
    if (player != Me()) {
        return;
    }
    Event event;

    event.granted = granted;
    event.page = page;
    event.slot = slot;
    event.purpose = purpose;
    if (data != nullptr) {
        std::memcpy(event.data, data, RecordSize(page));
    }
    g_events.push_back(event);
    if (claim && g_waiting > 0) {
        g_waiting--;
    }
}

// The put: into the cell asked for, or the page's first free one. -1: the board's page is full (the record stays the putter's: back in
// the hand, said).
int ApplyPut(int player, int page, int slot, const void *data, const char *how) {
    CStockItem *board = Board();

    if (board == nullptr || page < 0 || page > 2) {
        return -1;
    }
    // (a cell taken by the time the put lands: the page's first free one, nobody told. DC_COOP_MUTATE=storage_put_over (twin of
    // x-town-storage-clashput): the asked cell whatever it holds, the other's record written over, as the old apart slot clash did)
    int at = slot >= 0 && slot < PageCount(page) && (Empty(CellItem(board, page, slot)) || Mutated("storage_put_over")) ? slot : -1;

    for (int i = 0; at < 0 && i < PageCount(page); i++) {
        if (Empty(CellItem(board, page, i))) {
            at = i;
        }
    }
    char line[256];

    if (at < 0) {
        std::snprintf(line, sizeof line, "coop-storage: player %d's put of %s %d finds the storage's %s page full (%s): it stays theirs, back in the hand\n", player,
                      PageName(page), RecordItem(page, data), PageName(page), how);
        Said(line);
        Landed(player, true, page, slot, 0, data, false);
        return -1;
    }
    WriteCell(board, page, at, data);
    CoopWorldsBagDump("storage move"); // (DC_WORLDS_TEST_BAGDUMP; x-town-storage)
    std::snprintf(line, sizeof line, "coop-storage: player %d puts %s %d into the storage's %s slot %d (asked %d) at link tick %lld (%s)\n", player, PageName(page),
                  RecordItem(page, data), PageName(page), at, slot, (long long) NetplayLinkTick(), how);
    Said(line);
    return at;
}

// The claim: granted when the cell holds what the claimer saw; its record leaves the board (a swap's staged record takes its place).
bool ApplyClaim(int player, int page, int slot, int expected, int purpose, const void *swap, std::uint8_t *out, const char *how) {
    CStockItem *board = Board();
    char        line[256];

    if (board == nullptr || page < 0 || page > 2 || slot < 0 || slot >= PageCount(page)) {
        return false;
    }
    const int now = CellItem(board, page, slot);

    if (Empty(now) || now != expected) {
        std::snprintf(line, sizeof line, "coop-storage: player %d's claim of the storage's %s slot %d (%d seen) is refused at link tick %lld: it holds %d now (%s)\n", player,
                      PageName(page), slot, expected, (long long) NetplayLinkTick(), now, how);
        Said(line);
        return false;
    }
    ReadCell(board, page, slot, out);
    if (swap != nullptr) {
        WriteCell(board, page, slot, swap);
    } else {
        ClearCell(board, page, slot);
    }
    CoopWorldsBagDump("storage move"); // (DC_WORLDS_TEST_BAGDUMP; x-town-storage)
    std::snprintf(line, sizeof line, "coop-storage: player %d claims %s %d from the storage's %s slot %d at link tick %lld%s (%s)\n", player, PageName(page), now, PageName(page),
                  slot, (long long) NetplayLinkTick(), swap != nullptr ? ", their held record in its place" : purpose == 1 ? ", into their bag" : "", how);
    Said(line);
    return true;
}

void ApplySort(int player, int page, const char *how) {
    CStockItem *board = Board();

    if (board == nullptr) {
        return;
    }
    if (page == kPageItem) {
        board->SeitonItem();
    } else if (page == kPageAttach) {
        board->SeitonAttach();
    }
    char line[160];

    std::snprintf(line, sizeof line, "coop-storage: player %d sorts the storage's %s page at link tick %lld (%s)\n", player, PageName(page), (long long) NetplayLinkTick(), how);
    Said(line);
}

// ---- together: the lockstep commands (every copy, one tick) ----

void ApplyMoveCommand(int player, const NetCommand &command, std::int64_t) {
    if (player < 1 || player > kMaxPlayers) {
        return;
    }
    const int page = command.a & 3, what = (command.a >> 2) & 3;
    auto     &stage = g_stage[player];

    if (what == kMoveWord) {
        if (command.b < kMaxRecord / 4) {
            stage.resize(std::max<std::size_t>(stage.size(), command.b + 1u));
            stage[command.b] = command.c;
        }
        return;
    }
    if (what == kMoveSort) {
        ApplySort(player, page, "its lockstep command");
        return;
    }
    std::uint8_t data[kMaxRecord] = {};

    if ((command.a & 0x10) != 0) {
        std::memcpy(data, stage.data(), std::min<std::size_t>(stage.size() * 4, (std::size_t) RecordSize(page)));
    } else {
        std::memcpy(data, &command.c, 4);
    }
    stage.clear();
    ApplyPut(player, page, command.b, data, "its lockstep command");
}

void ApplyClaimCommand(int player, const NetCommand &command, std::int64_t) {
    if (player < 1 || player > kMaxPlayers) {
        return;
    }
    const int    page = command.a & 3, purpose = (command.a >> 2) & 1;
    const bool   swap = (command.a & 8) != 0;
    std::uint8_t staged[kMaxRecord] = {};
    std::uint8_t out[kMaxRecord] = {};

    if (swap) {
        std::memcpy(staged, g_stage[player].data(), std::min<std::size_t>(g_stage[player].size() * 4, (std::size_t) RecordSize(page)));
    }
    g_stage[player].clear();
    const bool granted = ApplyClaim(player, page, command.b, (std::int16_t) (command.c & 0xFFFF), purpose, swap ? staged : nullptr, out, "its lockstep command");

    Landed(player, granted, page, command.b, purpose, granted ? out : nullptr);
}

void QueueStaged(int page, const void *data, int size) {
    for (int i = 0; i * 4 < size; i++) {
        NetCommand command;

        command.kind = kCmdStorageMove;
        command.a = (std::uint8_t) (page | kMoveWord << 2);
        command.b = (std::uint16_t) i;
        std::memcpy(&command.c, static_cast<const std::uint8_t *>(data) + i * 4, 4);
        NetplayQueueCommand(command);
    }
}

// ---- apart: the host's word ----

constexpr std::uint32_t kStorageMagic = 0x44435757; // the worlds' messages (coop_worlds.cpp kMagic), told apart by the tag and size
constexpr std::uint32_t kStorageTag = 0x524F5453;   // "STOR"
enum : std::uint32_t { kAsk = 1, kAnswer = 2 };
enum : std::uint32_t { kOpPut = 1, kOpClaim = 2, kOpSort = 3 };
enum : std::uint32_t { kRefused = 0, kGranted = 1, kPlaced = 2, kKept = 3 };

#pragma pack(push, 1)
struct StorageMsg {
    std::uint32_t magic;
    std::uint32_t from; // the sender (netplay reads the second word as the sending player)
    std::uint32_t tag;
    std::uint32_t kind;
    std::uint32_t to;
    std::uint32_t id;       // the asker's count
    std::uint32_t op;
    std::int32_t  page, slot, expected, purpose, swap;
    std::uint32_t result;
    std::int32_t  placed;
    std::int32_t  len;
    std::uint8_t  bytes[kMaxRecord];
    std::uint8_t  spare[12]; // (a size no other worlds message has)
};
#pragma pack(pop)

struct Asked {
    StorageMsg                            msg;
    std::chrono::steady_clock::time_point sent;
};
std::map<std::uint32_t, Asked>                                         g_asks;    // the asker: its asks not answered
std::map<std::pair<std::uint32_t, std::uint32_t>, StorageMsg>          g_answers; // the host: each ask's answer, sent again for a repeat
std::uint32_t                                                          g_next_ask = 0;

constexpr int kHost = 1;

bool Apart() {
    return CoopWorldsApart();
}

// This copy's player: linked, the link's; apart (the link down), its role (the host 1, a joiner its number).
int Me() {
    return Apart() ? NetplayRole() : NetplayLocalPlayer();
}

void SendMsg(const StorageMsg &msg) {
    NetSocketSend(&msg, sizeof msg);
}

void Ask(std::uint32_t op, int page, int slot, int expected, int purpose, const void *data, int size, bool swap) {
    StorageMsg msg{};

    msg.magic = kStorageMagic;
    msg.tag = kStorageTag;
    msg.kind = kAsk;
    msg.from = (std::uint32_t) Me();
    msg.to = kHost;
    msg.id = ++g_next_ask;
    msg.op = op;
    msg.page = page;
    msg.slot = slot;
    msg.expected = expected;
    msg.purpose = purpose;
    msg.swap = swap ? 1 : 0;
    msg.len = size;
    if (data != nullptr && size > 0) {
        std::memcpy(msg.bytes, data, size);
    }
    g_asks[msg.id] = Asked{msg, std::chrono::steady_clock::now()};
    SendMsg(msg);
    std::fprintf(stderr, "coop-storage: player %u asks the host: %s of the storage's %s slot %d (ask %u)\n", msg.from,
                 op == kOpPut ? "a put" : op == kOpClaim ? "a claim" : "a sort", PageName(page), slot, msg.id);
    std::fflush(stderr);
}

// The host: an ask applied as its own press would be, and answered.
void HostTakes(const StorageMsg &ask) {
    const auto key = std::make_pair(ask.from, ask.id);

    if (auto it = g_answers.find(key); it != g_answers.end()) {
        SendMsg(it->second); // (a repeat: the same answer)
        return;
    }
    StorageMsg answer = ask;

    answer.kind = kAnswer;
    answer.from = kHost;
    answer.to = ask.from;
    answer.len = 0;
    const int player = (int) ask.from;

    if (ask.op == kOpPut) {
        const int at = ApplyPut(player, ask.page, ask.slot, ask.bytes, "the host's word apart");

        answer.result = at >= 0 ? kPlaced : kKept;
        answer.placed = at;
        if (at < 0) {
            answer.len = RecordSize(ask.page);
            std::memcpy(answer.bytes, ask.bytes, answer.len);
        }
    } else if (ask.op == kOpClaim) {
        std::uint8_t out[kMaxRecord] = {};
        const bool   granted = ApplyClaim(player, ask.page, ask.slot, ask.expected, ask.purpose, ask.swap != 0 ? ask.bytes : nullptr, out, "the host's word apart");

        answer.result = granted ? kGranted : kRefused;
        if (granted) {
            answer.len = RecordSize(ask.page);
            std::memcpy(answer.bytes, out, answer.len);
        }
    } else if (ask.op == kOpSort) {
        ApplySort(player, ask.page, "the host's word apart");
        answer.result = kPlaced;
    }
    g_answers[key] = answer;
    SendMsg(answer);
}

// The asker: the host's answer lands.
void AnswerLands(const StorageMsg &answer) {
    auto it = g_asks.find(answer.id);

    if (it == g_asks.end()) {
        return; // (a repeat)
    }
    const StorageMsg ask = it->second.msg;

    g_asks.erase(it);
    const int player = (int) answer.to;

    std::fprintf(stderr, "coop-storage: the host answers player %d's ask %u: %s\n", player, answer.id,
                 answer.result == kGranted ? "granted" : answer.result == kRefused ? "refused" : answer.result == kKept ? "the page full, back in the hand" : "placed");
    std::fflush(stderr);
    if (ask.op == kOpClaim) {
        if (answer.result == kGranted) {
            // (this copy's board as the host's now is, at once: the host's record of the cell comes after, alike)
            if (CStockItem *board = Board(); board != nullptr) {
                if (ask.swap != 0) {
                    WriteCell(board, ask.page, ask.slot, ask.bytes);
                } else {
                    ClearCell(board, ask.page, ask.slot);
                }
                CoopWorldsSlotRebase(ask.page, ask.slot);
                CoopWorldsBagDump("storage move"); // (DC_WORLDS_TEST_BAGDUMP; x-town-storage)
            }
            Landed(player, true, ask.page, ask.slot, ask.purpose, answer.bytes);
        } else {
            Landed(player, false, ask.page, ask.slot, ask.purpose, nullptr);
        }
    } else if (ask.op == kOpPut && answer.result == kKept) {
        Landed(player, true, ask.page, ask.slot, 0, answer.bytes, false);
    }
}

} // namespace

bool CoopStorageOn() {
    if (Mutated("storage_retail") || Me() == 0) {
        return false;
    }
    if (Apart()) {
        return true;
    }
    return CoopWorldsLinkedTogether() && FullMenuLocalOn() && CoopTownMenuRunsLocalHere(NetplayLocalPlayer());
}

void CoopStoragePut(int page, int slot, const void *data, int size) {
    if (Apart()) {
        if (Me() == kHost) {
            ApplyPut(kHost, page, slot, data, "the host's own apart");
        } else {
            Ask(kOpPut, page, slot, -1, 0, data, size, false);
        }
        return;
    }
    NetCommand command;

    command.kind = kCmdStorageMove;
    command.b = (std::uint16_t) slot;
    if (page == kPageItem) {
        command.a = (std::uint8_t) (page | kMoveCommit << 2);
        std::memcpy(&command.c, data, 4);
    } else {
        QueueStaged(page, data, size);
        command.a = (std::uint8_t) (page | kMoveCommit << 2 | 0x10);
    }
    NetplayQueueCommand(command);
}

void CoopStorageClaim(int page, int slot, int expected, int purpose, const void *swap, int swap_size) {
    g_waiting++;
    if (Apart()) {
        if (Me() == kHost) {
            std::uint8_t out[kMaxRecord] = {};
            const bool   granted = ApplyClaim(kHost, page, slot, expected, purpose, swap, out, "the host's own apart");

            Landed(kHost, granted, page, slot, purpose, granted ? out : nullptr);
        } else {
            Ask(kOpClaim, page, slot, expected, purpose, swap, swap != nullptr ? swap_size : 0, swap != nullptr);
        }
        return;
    }
    if (swap != nullptr) {
        QueueStaged(page, swap, swap_size);
    }
    NetCommand command;

    command.kind = kCmdStorageClaim;
    command.a = (std::uint8_t) (page | (purpose & 1) << 2 | (swap != nullptr ? 8 : 0));
    command.b = (std::uint16_t) slot;
    command.c = (std::uint32_t) (std::uint16_t) expected;
    // DC_COOP_MUTATE=storage_claim_local (twin of x-town-storage-clashtogether): the claim applied on the presser's copy at once, not
    // as a lockstep command: each copy grants its own player's lift, so two lifts of one cell both land and the boards part.
    if (Mutated("storage_claim_local")) {
        ApplyClaimCommand(Me(), command, NetplayLinkTick());
        return;
    }
    NetplayQueueCommand(command);
}

void CoopStorageSort(int page) {
    if (Apart()) {
        if (Me() == kHost) {
            ApplySort(kHost, page, "the host's own apart");
        } else {
            Ask(kOpSort, page, -1, -1, 0, nullptr, 0, false);
        }
        return;
    }
    NetCommand command;

    command.kind = kCmdStorageMove;
    command.a = (std::uint8_t) (page | kMoveSort << 2);
    NetplayQueueCommand(command);
}

bool CoopStorageWaiting() {
    return g_waiting > 0;
}

bool CoopStorageGranted(int *page, int *slot, int *purpose, void *data, int size) {
    if (g_events.empty() || !g_events.front().granted) {
        return false;
    }
    const Event event = g_events.front();

    g_events.pop_front();
    *page = event.page;
    *slot = event.slot;
    *purpose = event.purpose;
    std::memcpy(data, event.data, std::min(size, kMaxRecord));
    return true;
}

bool CoopStorageRefused() {
    if (g_events.empty() || g_events.front().granted) {
        return false;
    }
    g_events.pop_front();
    return true;
}

void CoopStorageTick() {
    const auto now = std::chrono::steady_clock::now();

    for (auto &[id, asked] : g_asks) {
        if (now - asked.sent > std::chrono::milliseconds(500)) {
            asked.sent = now;
            SendMsg(asked.msg);
        }
    }
}

void CoopStorageCommandHandlers() {
    NetplaySetCommandHandler(kCmdStorageClaim, ApplyClaimCommand);
    NetplaySetCommandHandler(kCmdStorageMove, ApplyMoveCommand);
}

bool CoopStorageReceive(const void *data, int size) {
    if (size != (int) sizeof(StorageMsg)) {
        return false;
    }
    StorageMsg msg;

    std::memcpy(&msg, data, sizeof msg);
    if (msg.magic != kStorageMagic || msg.tag != kStorageTag) {
        return false;
    }
    if (msg.to != (std::uint32_t) Me() && !(msg.kind == kAsk && Me() == kHost)) {
        return true; // (another player's)
    }
    if (msg.kind == kAsk && Me() == kHost) {
        HostTakes(msg);
    } else if (msg.kind == kAnswer) {
        AnswerLands(msg);
    }
    return true;
}

void CoopStorageReset() {
    g_events.clear();
    g_waiting = 0;
    for (auto &stage : g_stage) {
        stage.clear();
    }
}
