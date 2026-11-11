#include "coop/game_ns.hpp"
#include "coop/coop_partnerlog.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <random>
#include <string>
#include <system_error>
#include <vector>

#include "coop/netplay.hpp"
#include "coop/netplay_socket.hpp"
#include "coop/netplay_test.hpp"
#include "platform/paths.hpp"

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <io.h>
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

namespace {

constexpr std::uint32_t kDataMagic = 0x4443474C; // "LGCD": a piece of this copy's log
constexpr std::uint32_t kAckMagic = 0x4443414C;  // "LACD": the bytes of the other's log this copy holds
constexpr int           kPieceBytes = 1024;
constexpr std::size_t   kKeepBytes = 8u << 20;   // kept for the other copy while it does not acknowledge (the newest)
constexpr std::uint64_t kWindowBytes = 48 * 1024; // in flight, unacknowledged
constexpr double        kPaceBytes = 256.0 * 1024.0; // a second: a log's lines, never a catch-up's room
constexpr int           kReadEveryMs = 100;
constexpr int           kResendMs = 600; // no acknowledgement moved for this long: from the oldest unacknowledged again
constexpr int           kQuietMs = 3000; // and none at all for this long (the other copy away or gone): one piece a probe
constexpr int           kProbeMs = 2000;
constexpr int           kKeepFiles = 5;  // partner-log.1.txt to .5.txt: the earlier games' (a relaunch keeps them)

struct Data {
    std::uint32_t magic;
    std::uint32_t player;
    std::uint32_t run; // the sender's game run (a random number a start), so a relaunch starts its own section
    std::uint32_t length;
    std::uint64_t offset; // where these bytes start in the sender's log
    std::uint64_t base;   // the oldest byte the sender still keeps: below it the receiver can never have the rest
    std::uint8_t  bytes[kPieceBytes];
};

struct Ack {
    std::uint32_t magic;
    std::uint32_t player;
    std::uint32_t run; // the sender's run this acknowledges
    std::uint32_t unused;
    std::uint64_t held; // every byte below it is in this copy's partner-log
};

// One file the game's log goes to, read from where the last read stopped.
struct Source {
    std::string name;
#if defined(_WIN32)
    HANDLE file = INVALID_HANDLE_VALUE;
#else
    int file = -1;
#endif
};

struct Sender {
    bool                                  started = false;
    bool                                  off = false;
    std::uint32_t                         run = 0;
    std::vector<Source>                   sources;
    std::string                           kept;          // from base: read, not yet acknowledged
    std::uint64_t                         base = 0;      // the offset of kept's first byte (everything below is acknowledged or dropped)
    std::uint64_t                         next = 0;      // the next byte to send
    double                                tokens = 0.0;  // the pace's
    bool                                  linked = false; // the link has been up once: there is a partner to send to
    std::chrono::steady_clock::time_point read_at{}, token_at{}, moved_at{}, heard_at{};
} g_out;

struct Receiver {
    bool          any = false;
    std::uint32_t run = 0;
    std::uint64_t held = 0;
    std::uint64_t written = 0; // coop-catchupdrop: bytes of the other copy's log written in this game, all runs (the give-up hook's)
    std::FILE    *file = nullptr;
} g_in;

#if defined(_WIN32)
std::string Narrow(const wchar_t *text) {
    char out[1024];
    int  n = WideCharToMultiByte(CP_UTF8, 0, text, -1, out, sizeof out, nullptr, nullptr);
    return n > 0 ? std::string(out) : std::string();
}

// The file a CRT stream writes to, opened again for reading (a launcher's redirect shares reading). A console, a pipe or
// NUL (Play Dark Cloud Co-op.exe sends the log nowhere) has nothing to read.
void AddSource(int fd) {
    HANDLE handle = (HANDLE) _get_osfhandle(fd);

    if (handle == INVALID_HANDLE_VALUE || handle == nullptr || GetFileType(handle) != FILE_TYPE_DISK) {
        return;
    }

    wchar_t path[MAX_PATH * 2];
    DWORD   n = GetFinalPathNameByHandleW(handle, path, MAX_PATH * 2, FILE_NAME_NORMALIZED);

    if (n == 0 || n >= MAX_PATH * 2) {
        return;
    }
    const std::string name = Narrow(path);

    for (const Source &have : g_out.sources) {
        if (have.name == name) {
            return; // stderr sent to stdout's file (a pack's 2>&1, the harness's one stream)
        }
    }

    Source source;

    source.name = name;
    source.file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (source.file == INVALID_HANDLE_VALUE) {
        std::fprintf(stderr, "partnerlog: cannot read this copy's log %s (error %lu)\n", name.c_str(), GetLastError());
        return;
    }
    g_out.sources.push_back(source);
}

void ReadSource(Source &source, std::string &out) {
    char  chunk[16384];
    DWORD got = 0;

    while (ReadFile(source.file, chunk, sizeof chunk, &got, nullptr) && got > 0) {
        out.append(chunk, got);
    }
}
#else
void AddSource(int fd) {
    struct stat info;

    if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode)) {
        return;
    }

    char link[64];
    char path[4096];

    std::snprintf(link, sizeof link, "/proc/self/fd/%d", fd);
    const ssize_t n = readlink(link, path, sizeof path - 1);

    if (n <= 0) {
        return;
    }
    path[n] = '\0';
    for (const Source &have : g_out.sources) {
        if (have.name == path) {
            return;
        }
    }

    Source source;

    source.name = path;
    source.file = open(path, O_RDONLY);
    if (source.file < 0) {
        std::fprintf(stderr, "partnerlog: cannot read this copy's log %s\n", path);
        return;
    }
    g_out.sources.push_back(source);
}

void ReadSource(Source &source, std::string &out) {
    char    chunk[16384];
    ssize_t got;

    while ((got = read(source.file, chunk, sizeof chunk)) > 0) {
        out.append(chunk, (std::size_t) got);
    }
}
#endif

void Start() {
    g_out.started = true;
    g_out.off = NetplayMutated("partnerlog_off");
    g_out.run = std::random_device{}();
    if (g_out.run == 0) {
        g_out.run = 1;
    }
    std::fflush(stdout);
    std::fflush(stderr);
    AddSource(fileno(stdout));
    AddSource(fileno(stderr));

    std::string files;

    for (const Source &source : g_out.sources) {
        files += (files.empty() ? "" : ", ") + source.name;
    }
    if (g_out.off) {
        std::fprintf(stderr, "partnerlog: test: not sent (partnerlog_off)\n");
    } else if (files.empty()) {
        std::fprintf(stderr, "partnerlog: this copy's log goes to no file: nothing to send\n");
    } else {
        std::fprintf(stderr, "partnerlog: sending this copy's log (%s), run %08x\n", files.c_str(), g_out.run);
    }
    g_out.read_at = g_out.token_at = g_out.moved_at = g_out.heard_at = std::chrono::steady_clock::now();
}

// New bytes from the log files; past kKeepBytes unacknowledged the oldest go (the receiver writes how many).
void ReadNew() {
    for (Source &source : g_out.sources) {
        ReadSource(source, g_out.kept);
    }
    if (g_out.kept.size() > kKeepBytes) {
        const std::size_t drop = g_out.kept.size() - kKeepBytes;

        g_out.kept.erase(0, drop);
        g_out.base += drop;
        g_out.next = std::max(g_out.next, g_out.base);
    }
}

void SendAck(std::uint32_t run) {
    Ack ack{};

    ack.magic = kAckMagic;
    ack.player = (std::uint32_t) NetplayLinkPlayer();
    ack.run = run;
    ack.held = g_in.held;
    NetSocketSend(&ack, sizeof ack);
}

void Write(const char *text, std::size_t size) {
    if (g_in.file == nullptr) {
        // The first lines this game takes: the earlier games' files move up one (partner-log.1.txt the last one's).
        namespace fs = std::filesystem;
        const fs::path  folder = PathsSaveRoot();
        std::error_code error;

        fs::remove(folder / ("partner-log." + std::to_string(kKeepFiles) + ".txt"), error);
        for (int i = kKeepFiles - 1; i >= 1; i--) {
            fs::rename(folder / ("partner-log." + std::to_string(i) + ".txt"), folder / ("partner-log." + std::to_string(i + 1) + ".txt"), error);
        }
        fs::rename(folder / "partner-log.txt", folder / "partner-log.1.txt", error);
        g_in.file = std::fopen((folder / "partner-log.txt").string().c_str(), "wb");
        if (g_in.file == nullptr) {
            return;
        }
    }
    std::fwrite(text, 1, size, g_in.file);
    g_in.written += size;
}

void TakeData(const Data &data) {
    if (data.length > kPieceBytes) {
        return;
    }
    if (!g_in.any || data.run != g_in.run) {
        // A new run of the other copy's game (or the first heard): its own section from its byte 0.
        g_in.any = true;
        g_in.run = data.run;
        g_in.held = 0;

        char        line[256];
        std::time_t now = std::time(nullptr);
        char        when[32];

        std::strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", std::localtime(&now));
        const int n = std::snprintf(line, sizeof line, "==== player %u's log (run %08x), first heard %s ====\n", data.player, data.run, when);

        Write(line, (std::size_t) n);
        std::fprintf(stderr, "partnerlog: player %u's log (run %08x) to %s\n", data.player, data.run,
                     (PathsSaveRoot() / "partner-log.txt").string().c_str());
    }
    if (g_in.held < data.base) {
        char      line[128];
        const int n = std::snprintf(line, sizeof line, "[partner log: %llu bytes the other copy did not keep]\n",
                                    (unsigned long long) (data.base - g_in.held));

        Write(line, (std::size_t) n);
        g_in.held = data.base;
    }
    if (data.offset <= g_in.held && data.offset + data.length > g_in.held) {
        const std::uint64_t skip = g_in.held - data.offset;

        Write(reinterpret_cast<const char *>(data.bytes) + skip, (std::size_t) (data.length - skip));
        g_in.held = data.offset + data.length;
        if (g_in.file != nullptr) {
            std::fflush(g_in.file); // on disk at once: a crash of this copy keeps what came
        }
    }
    SendAck(data.run);
}

void TakeAck(const Ack &ack) {
    if (ack.run == g_out.run) {
        g_out.heard_at = std::chrono::steady_clock::now();
    }
    if (ack.run != g_out.run || ack.held <= g_out.base || ack.held > g_out.base + g_out.kept.size()) {
        return;
    }
    g_out.kept.erase(0, (std::size_t) (ack.held - g_out.base));
    g_out.base = ack.held;
    g_out.next = std::max(g_out.next, g_out.base);
    g_out.moved_at = std::chrono::steady_clock::now();
}

} // namespace

void PartnerLogTick() {
    // Only once the link has begun (this copy knows which player it is): before, a solo game has no partner.
    if (NetplayLinkPlayer() == 0) {
        return;
    }
    if (!g_out.started) {
        Start();
    }
    if (g_out.off || g_out.sources.empty()) {
        return;
    }

    auto now = std::chrono::steady_clock::now();

    if (now - g_out.read_at >= std::chrono::milliseconds(kReadEveryMs)) {
        g_out.read_at = now;
        ReadNew();
    }

    const double seconds = std::chrono::duration<double>(now - g_out.token_at).count();

    g_out.token_at = now;
    g_out.tokens = std::min(g_out.tokens + seconds * kPaceBytes, (double) kWindowBytes);

    // Nothing goes before the link has been up once (a host waiting for its partner would send to an address nobody
    // answers at); the lines read meanwhile wait in kept.
    if (NetplayLinkUp()) {
        if (!g_out.linked) {
            g_out.heard_at = now;
        }
        g_out.linked = true;
    }
    if (!g_out.linked) {
        g_out.moved_at = now;
        return;
    }

    // A catch-up's pieces have the link to themselves (their pacer measures it); the lines wait. coop-catchupdrop (Jep's
    // 2026-11-04 pack 8: her copy waited 2 min for his page request, his stuck, and none of her lines came till the link
    // dropped): only while its pieces move (NetplayCatchMoving); a catch-up waiting on the other copy lets them go.
    if (NetplayCatchUnderWay() && NetplayCatchMoving()) {
        g_out.moved_at = now;
        return;
    }

    // Whole lines only.
    const std::size_t   last = g_out.kept.rfind('\n');
    const std::uint64_t end = last == std::string::npos ? g_out.base : g_out.base + last + 1;

    // Unheard for kQuietMs (the link down, the other copy apart and away, or gone): one piece every kProbeMs until it
    // answers, not a window every kResendMs.
    const bool          quiet = now - g_out.heard_at >= std::chrono::milliseconds(kQuietMs);
    const std::uint64_t window = quiet ? kPieceBytes : kWindowBytes;

    if (g_out.next == g_out.base) {
        g_out.moved_at = now; // nothing in flight: no acknowledgement is late
    } else if (now - g_out.moved_at >= std::chrono::milliseconds(quiet ? kProbeMs : kResendMs)) {
        g_out.next = g_out.base; // lost on the way, or the other copy is away: from the oldest again
        g_out.moved_at = now;
    }

    while (g_out.next < end && g_out.next - g_out.base < window) {
        Data data{};

        data.length = (std::uint32_t) std::min<std::uint64_t>(kPieceBytes, end - g_out.next);
        if (g_out.tokens < data.length) {
            break;
        }
        data.magic = kDataMagic;
        data.player = (std::uint32_t) NetplayLinkPlayer();
        data.run = g_out.run;
        data.offset = g_out.next;
        data.base = g_out.base;
        std::memcpy(data.bytes, g_out.kept.data() + (g_out.next - g_out.base), data.length);
        if (!NetSocketSend(&data, sizeof data)) {
            break;
        }
        g_out.tokens -= data.length;
        g_out.next += data.length;
    }
}

std::uint64_t PartnerLogTakenBytes() {
    return g_in.written;
}

bool PartnerLogReceive(std::uint32_t magic, const void *data, int size) {
    if (magic == kDataMagic) {
        if (size == (int) sizeof(Data)) {
            Data piece;

            std::memcpy(&piece, data, sizeof piece);
            TakeData(piece);
        }
        return true;
    }
    if (magic == kAckMagic) {
        if (size == (int) sizeof(Ack)) {
            Ack ack;

            std::memcpy(&ack, data, sizeof ack);
            TakeAck(ack);
        }
        return true;
    }
    return false;
}
