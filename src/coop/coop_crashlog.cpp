// coop-worlds: DC_WORLDS_TEST_CRASHLOG=1 (tests only): an access violation logs its address and the exe's base, so
// llvm-addr2line can name the faulting line from a test log (lesson 28: a crash should leave a record).
#ifdef _WIN32
#include "coop/game_ns.hpp"
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <chrono>
#include <thread>

int  TestEnvInt(const char *name, int fallback); // coop_core.cpp
void QaLogHold(bool hold);

namespace {

LONG CALLBACK CrashLog(EXCEPTION_POINTERS *info) {
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    const auto address = reinterpret_cast<ULONG_PTR>(info->ExceptionRecord->ExceptionAddress);
    const auto base = reinterpret_cast<ULONG_PTR>(GetModuleHandleW(nullptr));

    QaLogHold(false); // (WS5: the lines of his lockstep, if held)
    std::fprintf(stderr, "crashlog: access violation at 0x%llx (exe base 0x%llx, offset 0x%llx), %s 0x%llx\n", (unsigned long long) address,
                 (unsigned long long) base, (unsigned long long) (address - base),
                 info->ExceptionRecord->ExceptionInformation[0] == 1 ? "writing" : "reading",
                 (unsigned long long) info->ExceptionRecord->ExceptionInformation[1]);
    // coop-snapjoin: the exe's frames on the stack (the handler runs on the faulting thread's stack), as image offsets for
    // the link map (darkcloud.map), so a fault inside a system DLL still names the game's caller.
    void        *frames[48];
    const USHORT count = CaptureStackBackTrace(0, 48, frames, nullptr);

    std::fprintf(stderr, "crashlog: exe frames:");
    for (USHORT i = 0; i < count; i++) {
        const auto at = reinterpret_cast<ULONG_PTR>(frames[i]);

        if (at >= base && at - base < 0x10000000) {
            std::fprintf(stderr, " +0x%llx", (unsigned long long) (at - base));
        }
    }
    // The faulting thread's own stack (CaptureStackBackTrace walks the handler's, which stops at the dispatcher): words on
    // it that point into the exe, the return addresses among them (the first is a leaf function's caller).
    std::fprintf(stderr, "\ncrashlog: exe words on the stack:");
    const auto *stack = reinterpret_cast<const ULONG_PTR *>(info->ContextRecord->Rsp);
    int         found = 0;

    for (int i = 0; i < 512 && found < 24; i++) {
        if (stack[i] >= base + 0x1000 && stack[i] - base < 0x400000) {
            std::fprintf(stderr, " +0x%llx", (unsigned long long) (stack[i] - base));
            found++;
        }
    }
    // coop-stage4: the faulting thread's call chain unwound by its unwind tables (RtlVirtualUnwind from the fault's context):
    // the exact callers where the words above are stale or ambiguous.
    std::fprintf(stderr, "\ncrashlog: exe unwound:");
    CONTEXT context = *info->ContextRecord;

    for (int depth = 0; depth < 32 && context.Rip != 0; depth++) {
        if (context.Rip >= base && context.Rip - base < 0x10000000) {
            std::fprintf(stderr, " +0x%llx", (unsigned long long) (context.Rip - base));
        }
        DWORD64           image = 0;
        PRUNTIME_FUNCTION entry = RtlLookupFunctionEntry(context.Rip, &image, nullptr);

        if (entry == nullptr) {
            context.Rip = *reinterpret_cast<const DWORD64 *>(context.Rsp);
            context.Rsp += 8;
        } else {
            void   *handler_data = nullptr;
            DWORD64 establisher = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, context.Rip, entry, &context, &handler_data, &establisher, nullptr);
        }
    }
    std::fprintf(stderr, "\n");
    std::fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}

struct Install {
    Install() {
        const char *on = std::getenv("DC_WORLDS_TEST_CRASHLOG");

        if (on != nullptr && on[0] == '1') {
            AddVectoredExceptionHandler(0, CrashLog);
        }
    }
} g_install;

} // namespace

// WS5 (DC_NETPLAY_TEST_STALL_WATCH): the game thread stopped for a moment from the watch's thread: where it is (the module and
// offset of each frame of its call chain), for darkcloud.map.
static HANDLE g_stall_thread = nullptr;

void CoopStallAdopt() {
    g_stall_thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, GetCurrentThreadId());
}

// (the module and offset of an address, for the line)
static void StallWhere(ULONG_PTR address, char *out, size_t size) {
    HMODULE module = nullptr;
    char    name[260] = "?";

    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<const char *>(address), &module)) {
        GetModuleFileNameA(module, name, sizeof name);
    }
    const char *leaf = name;

    for (const char *at = name; *at != '\0'; at++) {
        if (*at == 92 || *at == 47) { // (a backslash or a slash)
            leaf = at + 1;
        }
    }
    std::snprintf(out, size, "%s+0x%llx", leaf, (unsigned long long) (address - reinterpret_cast<ULONG_PTR>(module)));
}

// (the stopped game thread's call chain into frames: up to 32 return addresses; 0 when it could not be stopped)
static int StallStack(ULONG_PTR *frames) {
    if (g_stall_thread == nullptr) {
        return 0;
    }
    // Its real call chain, unwound by the exe's and the system's unwind tables while it stays stopped (the stack scan before
    // this read stale words from the C runtime's write buffer). Only addresses are kept here: no heap, no stderr, nothing the
    // stopped thread may hold.
    int     count = 0;
    CONTEXT context{};

    context.ContextFlags = CONTEXT_FULL;
    if (SuspendThread(g_stall_thread) == (DWORD) -1) {
        return 0;
    }
    if (GetThreadContext(g_stall_thread, &context)) {
        while (count < 32 && context.Rip != 0) {
            frames[count++] = context.Rip;
            DWORD64 image = 0;
            PRUNTIME_FUNCTION entry = RtlLookupFunctionEntry(context.Rip, &image, nullptr);

            if (entry == nullptr) { // (a leaf: its return address is at the stack's top)
                context.Rip = *reinterpret_cast<DWORD64 *>(context.Rsp);
                context.Rsp += 8;
                continue;
            }
            PVOID   data = nullptr;
            DWORD64 frame = 0;

            RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, context.Rip, entry, &context, &data, &frame, nullptr);
        }
    }
    ResumeThread(g_stall_thread);
    return count;
}

// (the chain as one write: word by word, each word a write on a full PC, the watch itself held the game thread's log lines)
static void StallSay(const char *head, const ULONG_PTR *frames, int count) {
    char line[1400];
    int  used = std::snprintf(line, sizeof line, "%s", head);

    for (int i = 0; i < count && used < (int) sizeof line - 80; i++) {
        char at[60];

        StallWhere(frames[i], at, sizeof at);
        used += std::snprintf(line + used, sizeof line - used, " %s", at);
    }
    std::fprintf(stderr, "%s\n", line);
}

void CoopStallSample(const char *where, bool inside, long long quiet_ms, long ticks, long long calls) {
    ULONG_PTR frames[32] = {};
    const int count = StallStack(frames);

    if (count == 0) {
        return;
    }
    char head[300];

    std::snprintf(head, sizeof head, "stall-watch: %lld ms with no replay tick (tick %ld, %lld calls): at \"%s\", %s the netplay tick; stack:", quiet_ms, ticks, calls, where,
                  inside ? "inside" : "outside");
    StallSay(head, frames, count);
}

// WS5 (DC_COOP_TEST_FRAME_WATCH=<ms>): no frame drawn on this copy for <ms> around a merge: the game thread's call chain, each
// 40 ms of the wait. Off unless the test switch is set: a player's copy starts no thread (l-local-play-houseout-slowreplay's
// 250-280 ms on her screen, which no log line placed).
static std::atomic<long long> g_frame_beat{0};
static std::atomic<long long> g_frame_tick{0};
static std::atomic<bool>      g_frame_armed{false};

static long long FrameNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

static void FrameWatchRun(int limit_ms) {
    long long said_at = 0;
    int       said = 0;

    for (;;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        const long long beat = g_frame_beat.load(), now = FrameNowMs();

        if (!g_frame_armed.load() || beat == 0 || now - beat < limit_ms || now - said_at < 40 || said >= 200) {
            continue;
        }
        ULONG_PTR frames[32] = {};
        const int count = StallStack(frames);

        said_at = now;
        said++;
        if (count > 0) {
            char head[120];

            std::snprintf(head, sizeof head, "frame-watch: %lld ms with no frame drawn (link tick %lld) [t %lld]; stack:", now - beat, g_frame_tick.load(), now % 10000000);
            StallSay(head, frames, count);
        }
    }
}

void CoopFrameBeat(bool armed, long long link_tick) {
    static const int limit_ms = TestEnvInt("DC_COOP_TEST_FRAME_WATCH", 0);
    static bool      started = false;

    if (limit_ms <= 0) {
        return;
    }
    if (!started) {
        started = true;
        CoopStallAdopt();
        std::thread(FrameWatchRun, limit_ms).detach();
    }
    g_frame_tick = link_tick;
    g_frame_beat = FrameNowMs();
    g_frame_armed = armed;
}
#else
void CoopStallAdopt() {}
void CoopStallSample(const char *, bool, long long, long, long long) {}
void CoopFrameBeat(bool, long long) {}
#endif
