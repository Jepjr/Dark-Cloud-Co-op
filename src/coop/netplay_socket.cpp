// No game header here. On Windows the system's socket headers fail their own layout checks under the
// game's types.h (which port.h puts ahead of every unit), so the few Winsock calls the link makes are
// declared here, with the address laid out as Winsock lays out sockaddr_in.
#include "coop/game_ns.hpp"
#include "coop/netplay_socket.hpp"

#include "coop/coop.hpp"
#include "coop/netplay_steam.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <mutex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <utility>
#include <vector>

#if defined(_WIN32)
extern "C" {
__declspec(dllimport) int __stdcall WSAStartup(unsigned short version, void *data);
__declspec(dllimport) std::uintptr_t __stdcall socket(int family, int type, int protocol);
__declspec(dllimport) int __stdcall bind(std::uintptr_t socket, const void *address, int size);
__declspec(dllimport) int __stdcall sendto(std::uintptr_t socket, const char *data, int size, int flags, const void *to, int to_size);
__declspec(dllimport) int __stdcall recvfrom(std::uintptr_t socket, char *data, int size, int flags, void *from, int *from_size);
__declspec(dllimport) int __stdcall ioctlsocket(std::uintptr_t socket, long command, unsigned long *argument);
__declspec(dllimport) int __stdcall connect(std::uintptr_t socket, const void *address, int size);
__declspec(dllimport) int __stdcall getsockname(std::uintptr_t socket, void *address, int *size);
__declspec(dllimport) int __stdcall closesocket(std::uintptr_t socket);
__declspec(dllimport) int __stdcall setsockopt(std::uintptr_t socket, int level, int name, const char *value, int size);
struct addrinfo;
__declspec(dllimport) int __stdcall getaddrinfo(const char *node, const char *service, const addrinfo *hints, addrinfo **out);
__declspec(dllimport) void __stdcall freeaddrinfo(addrinfo *info);
__declspec(dllimport) int __stdcall SetHandleInformation(void *object, unsigned long mask, unsigned long flags); // (kernel32)
}
#else
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#endif

#if defined(_WIN32)
#define ADDRESS(at) ((const void *) (at))
#else
#define ADDRESS(at) (reinterpret_cast<const sockaddr *>(at))
#endif

bool NetplayMutated(const char *name);   // netplay_test.cpp (its header holds game types)

namespace {
// A NAT punch's bytes (NetSocketPunch).
// The last NAT code a Steam guest sent (NetSocketNatHeard).
std::string g_nat_heard;
constexpr char kPunch[4] = {'D', 'C', 'N', 'P'};

struct Address {
    std::uint16_t family;
    std::uint16_t port;
    std::uint32_t address;
    std::uint8_t  zero[8];
};

static_assert(sizeof(Address) == 16);

#if defined(_WIN32)
constexpr std::uintptr_t kNoSocket = ~std::uintptr_t{0};
std::uintptr_t           g_socket = kNoSocket;
#else
constexpr int kNoSocket = -1;
int           g_socket = kNoSocket;
#endif

Address g_peer{};

// With no peer given: the sender of the datagram just received, the one kept (a Hello's), and its
// address as text.
bool    g_learn = false;
Address g_from{};
Address g_kept{};
char    g_peer_text[32] = "";

// Over Steam (netplay_steam.hpp): the peer's SteamID instead of an address (0: UDP), and the sender's.
std::uint64_t g_peer_steam = 0;
std::uint64_t g_from_steam = 0;
std::uint64_t g_kept_steam = 0;

std::uint16_t NetOrder(std::uint16_t value) {
    return (std::uint16_t) ((value >> 8) | (value << 8));
}

// a.b.c.d into the four bytes in network order, as inet_pton gives them.
bool ParseAddress(const char *text, std::uint32_t &out) {
    unsigned char bytes[4];
    const char   *at = text;

    for (int i = 0; i < 4; i++) {
        char *end = nullptr;
        long  part = std::strtol(at, &end, 10);

        if (end == at || part < 0 || part > 255 || (i < 3 && *end != '.') || (i == 3 && *end != '\0')) {
            return false;
        }

        bytes[i] = (unsigned char) part;
        at = end + 1;
    }

    std::memcpy(&out, bytes, 4);
    return true;
}

} // namespace

namespace {

struct Held {
    std::chrono::steady_clock::time_point due;
    std::vector<char>                     data;
};

struct Impair {
    bool              on = false;
    int               lag_ms = 0;
    int               jitter_ms = 0;
    double            loss = 0.0;
    std::mt19937      random{1};
    std::vector<Held> held;
    long long         sent = 0;
    long long         dropped = 0;
};

Impair g_impair;

// coop-punchlog: UDP datagrams this socket sent (punches, Hellos, link data) and heard from anyone but the STUN server
// (punches counted), and the last one's sender, for a NAT join's log (NetSocketUdpCounts).
// DC_NETPLAY_TEST_NAT_BLOCK=1 (test hook): every datagram but STUN's dropped as it comes, as routers that never let
// the other side's punch in (the fallback to Steam then carries the link).
long long g_udp_sent = 0, g_udp_heard = 0;
Address   g_udp_from{};
// coop-pt1108s (Jep's 11-08 playtest: "port forward open (proven)" on any datagram): a datagram proves the port open only from an
// address this socket never sent to (a reply to our own punch or Hello comes through the hole we made). g_udp_fresh counts those,
// g_udp_fresh_from is the last one's sender. DC_NETPLAY_MUTATE=port_proof_any (twin): every datagram counts, as before.
std::vector<std::pair<std::uint32_t, std::uint16_t>> g_sent_to;
long long g_udp_fresh = 0;
Address   g_udp_fresh_from{};
void NoteSentTo(const Address &to) {
    const auto key = std::make_pair((std::uint32_t) to.address, (std::uint16_t) to.port);

    if (std::find(g_sent_to.begin(), g_sent_to.end(), key) == g_sent_to.end() && g_sent_to.size() < 256) {
        g_sent_to.push_back(key);
    }
}
bool      g_test_cut = false; // coop-natonly: NetSocketTestCut, the same drop from a moment on (a link cut mid-play)
bool      g_steam_codes_only = false; // coop-natonly: NetSocketSteamCodesOnly

// False only when Steam refused the datagram for a full send buffer (coop-steamrate).
bool SendNow(const void *data, std::size_t size) {
    if (g_peer_steam != 0) {
        return NetSteamSend(g_peer_steam, data, size) != -1;
    }

    if (g_peer.address == 0) {
        return true;
    }

    sendto(g_socket, (const char *) data, (int) size, 0, ADDRESS(&g_peer), sizeof g_peer);
    NoteSentTo(g_peer);
    g_udp_sent++;
    return true;
}

// Sends what the injected lag held and is now due.
void Release() {
    auto now = std::chrono::steady_clock::now();

    for (std::size_t i = 0; i < g_impair.held.size();) {
        if (g_impair.held[i].due <= now) {
            SendNow(g_impair.held[i].data.data(), g_impair.held[i].data.size());
            g_impair.held.erase(g_impair.held.begin() + (std::ptrdiff_t) i);
        } else {
            i++;
        }
    }
}

} // namespace

namespace {
// The port the socket is bound to: opened again on it (a NAT code's early open at the title, then the link), it keeps
// the socket, so the router's mapping a STUN answer and a punch made stays the link's.
int g_bound_port = 0;
} // namespace


bool NetSocketOpen(int listen_port, const char *peer_text, int peer_port) {
    constexpr int kInet = 2, kDatagram = 2, kUdp = 17;

    // "a.b.c.d:port": a peer at that port (a NAT code's mapped port), not the player's usual one.
    std::string peer_host = peer_text != nullptr ? peer_text : "";
    if (std::size_t colon = peer_host.rfind(':'); colon != std::string::npos && peer_host.rfind("steam:", 0) != 0) {
        peer_port = std::atoi(peer_host.c_str() + colon + 1);
        peer_host.resize(colon);
    }
    const char *peer = peer_text != nullptr ? peer_host.c_str() : nullptr;

    if (g_socket != kNoSocket && g_bound_port == listen_port) {
        std::fprintf(stderr, "netplay: keeping the socket on UDP port %d\n", listen_port);
    } else {
#if defined(_WIN32)
    alignas(8) unsigned char wsa[1024];

    if (WSAStartup(0x0202, wsa) != 0) {
        std::fprintf(stderr, "netplay: WSAStartup failed\n");
        return false;
    }
#endif

    g_socket = socket(kInet, kDatagram, kUdp);

    if (g_socket == kNoSocket) {
        std::fprintf(stderr, "netplay: socket failed\n");
        return false;
    }

    // coop-sockinherit (train 49's survey, 2026-11-03: x-floor-gate-b4story's player 1 "cannot listen on UDP port 61401", the
    // port of the row before it, mp-nat-split~jumpplace): a socket is inheritable by default, and the processes this copy starts
    // take its handles (dcsteam.exe, CreateProcess with inheritance on for its log; Local's player 2), so the Steam helper held
    // this copy's port open after the game had gone, through its own Steam shutdown, and the next copy on that port could not
    // listen. The port is this process's alone. DC_NETPLAY_MUTATE=socket_inherited (twin): inheritable, as before.
    const bool kept = !NetplayMutated("socket_inherited");
#if defined(_WIN32)
    if (kept) {
        SetHandleInformation(reinterpret_cast<void *>(g_socket), 1, 0); // HANDLE_FLAG_INHERIT off
    }
#else
    if (kept) {
        fcntl(g_socket, F_SETFD, fcntl(g_socket, F_GETFD, 0) | FD_CLOEXEC);
    }
#endif

    Address local{};
    local.family = kInet;
    local.port = NetOrder((std::uint16_t) listen_port);

    // coop-fwprompt (Jep, 2026-10-30): DC_NETPLAY_BIND=loopback listens on 127.0.0.1 alone. The harness sets it for
    // every test copy: each run starts from a fresh copy of the exe, and Windows asks about the firewall for every new
    // exe that listens on the network, never for one on loopback only. Players never set it. A test whose copies meet at
    // another loopback address (127.0.0.2: mp-direct-join's relay, n-join-partner's typed address) names it instead; only
    // 127.x.x.x is taken. DC_NETPLAY_MUTATE=bind_any is the twin.
    const char *bind_to = std::getenv("DC_NETPLAY_BIND");
    unsigned    part[4] = {127, 0, 0, 1};
    const bool  named = bind_to != nullptr && std::sscanf(bind_to, "%u.%u.%u.%u", &part[0], &part[1], &part[2], &part[3]) == 4 &&
                       part[0] == 127 && part[1] < 256 && part[2] < 256 && part[3] < 256;
    if (bind_to != nullptr && (named || std::strcmp(bind_to, "loopback") == 0) && !NetplayMutated("bind_any")) {
        if (!named) {
            part[0] = 127; part[1] = 0; part[2] = 0; part[3] = 1;
        }
        const std::uint8_t loopback[4] = {(std::uint8_t) part[0], (std::uint8_t) part[1], (std::uint8_t) part[2], (std::uint8_t) part[3]};
        std::memcpy(&local.address, loopback, sizeof loopback);
        std::fprintf(stderr, "netplay: listening on loopback only (DC_NETPLAY_BIND %u.%u.%u.%u)\n", part[0], part[1], part[2], part[3]);
    }

    if (bind(g_socket, ADDRESS(&local), sizeof local) != 0) {
        std::fprintf(stderr, "netplay: cannot listen on UDP port %d\n", listen_port);
        return false;
    }
    g_bound_port = listen_port;
    std::fprintf(stderr, "netplay: socket on UDP port %d, %s\n", listen_port, kept ? "kept from the processes it starts" : "inheritable (socket_inherited)");

    // coop-mergetime: room for a window of a resync's or catch-up's pieces (up to 2 MB) arriving between two reads of a
    // copy that reads once a frame; the default 64 KB lost most of a burst.
    const int buffer = 4 << 20;
#if defined(_WIN32)
    setsockopt(g_socket, 0xFFFF, 0x1002, (const char *) &buffer, sizeof buffer); // SOL_SOCKET, SO_RCVBUF
    setsockopt(g_socket, 0xFFFF, 0x1001, (const char *) &buffer, sizeof buffer); // SO_SNDBUF
#else
    setsockopt(g_socket, SOL_SOCKET, SO_RCVBUF, &buffer, sizeof buffer);
    setsockopt(g_socket, SOL_SOCKET, SO_SNDBUF, &buffer, sizeof buffer);
#endif

#if defined(_WIN32)
    unsigned long nonblocking = 1;
    ioctlsocket(g_socket, (long) 0x8004667E, &nonblocking); // FIONBIO
#else
    fcntl(g_socket, F_SETFL, fcntl(g_socket, F_GETFL, 0) | O_NONBLOCK);
#endif
    }

    g_peer.family = kInet;
    g_peer.port = NetOrder((std::uint16_t) peer_port);

    g_learn = peer == nullptr || *peer == '\0' || std::strcmp(peer, "any") == 0;

    g_peer_steam = 0;

    if (!g_learn && std::strncmp(peer, "steam:", 6) == 0) {
        // A Steam host (one client's "Steam friend" join): its SteamID, through Valve's relays.
        g_peer_steam = std::strtoull(peer + 6, nullptr, 10);
        g_peer.address = 0;
        std::snprintf(g_peer_text, sizeof g_peer_text, "%s", peer);

        if (g_peer_steam == 0 || !NetSteamStart()) {
            std::fprintf(stderr, "netplay: cannot reach %s: %s\n", peer, NetSteamStatus());
            return false;
        }
    } else if (g_learn) {
        g_peer.address = 0;
    } else if (!ParseAddress(peer, g_peer.address)) {
        std::fprintf(stderr, "netplay: bad peer address %s (four numbers, like 127.0.0.1)\n", peer);
        return false;
    }

    g_impair.lag_ms = TestEnvInt("DC_NETPLAY_LAG_MS", 0);
    g_impair.jitter_ms = TestEnvInt("DC_NETPLAY_JITTER_MS", 0);
    g_impair.loss = TestEnvInt("DC_NETPLAY_LOSS_PCT", 0) / 100.0;
    g_impair.random.seed((unsigned) (TestEnvInt("DC_NETPLAY_NET_SEED", 0) * 2 + listen_port));
    g_impair.on = g_impair.lag_ms > 0 || g_impair.jitter_ms > 0 || g_impair.loss > 0.0;

    if (g_impair.on) {
        std::fprintf(stderr, "netplay: test: sending with %d ms lag, %d ms jitter, %.0f%% loss\n", g_impair.lag_ms, g_impair.jitter_ms,
                     g_impair.loss * 100.0);
    }

    return true;
}

namespace {

// coop-apartsync: the end of a test's network blackout (NetSocketBlackout): nothing leaves this copy until then.
std::atomic<long long> g_blackout_until_ms{0};

long long SteadyMs() {
    return (long long) std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace

void NetSocketBlackout(int ms) {
    g_blackout_until_ms = SteadyMs() + ms;
    std::fprintf(stderr, "netplay: test: network blackout for %d ms (nothing sent)\n", ms);
    std::fflush(stderr);
}

// The link's keepalive thread sends too, and the joiner's catch-up receiver sends and reads (netplay.cpp): the send path and the
// test impairment's held datagrams (Release, from either path) under one lock.
static std::mutex g_send_lock;

bool NetSocketSend(const void *data, std::size_t size) {
    std::lock_guard<std::mutex> hold(g_send_lock);

    g_impair.sent++;

    if (g_blackout_until_ms.load() != 0 && SteadyMs() < g_blackout_until_ms.load()) {
        g_impair.dropped++;
        return true;
    }

    if (!g_impair.on) {
        return SendNow(data, size);
    }

    Release();

    if (std::uniform_real_distribution<double>(0.0, 1.0)(g_impair.random) < g_impair.loss) {
        g_impair.dropped++;
        return true;
    }

    int  jitter = g_impair.jitter_ms > 0 ? std::uniform_int_distribution<int>(0, g_impair.jitter_ms)(g_impair.random) : 0;
    Held held;

    held.due = std::chrono::steady_clock::now() + std::chrono::milliseconds(g_impair.lag_ms + jitter);
    held.data.assign((const char *) data, (const char *) data + size);
    g_impair.held.push_back(std::move(held));
    return true;
}

bool NetSocketLearnsPeer() {
    return g_learn;
}

void NetSocketKeepSender() {
    g_kept = g_from;
    g_kept_steam = g_from_steam;
}

const char *NetSocketAnswerHello() {
    if (g_learn && g_kept_steam != 0) {
        g_peer_steam = g_kept_steam;
        std::snprintf(g_peer_text, sizeof g_peer_text, "steam:%llu", (unsigned long long) g_peer_steam);
    } else if (g_learn && g_kept.address != 0) {
        g_peer_steam = 0;
        g_peer = g_kept;

        const auto *bytes = (const unsigned char *) &g_peer.address;
        std::snprintf(g_peer_text, sizeof g_peer_text, "%u.%u.%u.%u:%u", bytes[0], bytes[1], bytes[2], bytes[3],
                      (unsigned) NetOrder(g_peer.port));
    }

    return g_peer_text;
}

const char *NetSocketPeerText() {
    return g_peer_text;
}

void NetSocketCounts(long long &sent, long long &dropped) {
    sent = g_impair.sent;
    dropped = g_impair.dropped;
}

bool NetSocketQueue(int &pending_bytes, int &rate) {
    pending_bytes = rate = 0;
    return g_peer_steam != 0 && NetSteamQueue(g_peer_steam, pending_bytes, rate);
}

bool NetSocketSteamLine(char *line, std::size_t size) {
    return g_peer_steam != 0 && NetSteamLine(g_peer_steam, line, size);
}

namespace {

// STUN (RFC 5389): one Binding request from the link's own socket asks a public server which address and port
// this PC's datagrams leave the router with: the host's internet (WAN) address, shown for a friend outside the
// network. Asynchronous (a thread resolves the server and sends three requests); the answer is taken from the
// link's socket as it reads (NetSocketReceive), never handed to the link.
constexpr std::uint32_t kStunCookie = 0x2112A442;
std::atomic<int>        g_stun_state{0}; // 0 not asked, 1 asking, 2 answered, 3 no answer
Address                 g_stun_server{};
unsigned char           g_stun_id[12];
char                    g_wan[24] = "";
int                     g_wan_port = 0;
int                     g_local_port = 0;
std::thread             g_stun_thread;          // coop-cand58: joined as the game ends (NetSocketStunStop)
std::atomic<bool>       g_stun_stop{false};
// (its waits, cut short by NetSocketStunStop)
void StunWait(int ms) {
    for (int waited = 0; waited < ms && !g_stun_stop.load(); waited += 20) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

struct AddressInfo { // addrinfo's layout (Windows and Linux on x64 agree up to ai_addr)
    int           flags, family, socktype, protocol;
    std::size_t   addrlen;
#if defined(_WIN32)
    char         *canonname;
    Address      *addr;
#else
    Address      *addr;
    char         *canonname;
#endif
    AddressInfo  *next;
};

bool Resolve(const std::string &host, std::uint32_t &out) {
    if (ParseAddress(host.c_str(), out)) {
        return true;
    }
    AddressInfo hints{};
    hints.family = 2; // IPv4
    hints.socktype = 2;
    AddressInfo *found = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, reinterpret_cast<const addrinfo *>(&hints), reinterpret_cast<addrinfo **>(&found)) != 0 || found == nullptr) {
        return false;
    }
    out = found->addr->address;
    freeaddrinfo(reinterpret_cast<addrinfo *>(found));
    return true;
}

// The answer, if data is one: its XOR-MAPPED-ADDRESS (or plain MAPPED-ADDRESS).
bool TakeStun(const unsigned char *data, int size) {
    if (size < 20 || data[0] != 0x01 || data[1] != 0x01 || std::memcmp(data + 8, g_stun_id, 12) != 0) {
        return false;
    }
    std::uint32_t cookie = (std::uint32_t) data[4] << 24 | (std::uint32_t) data[5] << 16 | (std::uint32_t) data[6] << 8 | data[7];
    if (cookie != kStunCookie || TestEnvIs("DC_NETPLAY_MUTATE", "stun_ignored")) {
        return cookie == kStunCookie;
    }
    int length = data[2] << 8 | data[3];
    for (int at = 20; at + 4 <= 20 + length && at + 4 <= size;) {
        int type = data[at] << 8 | data[at + 1];
        int len = data[at + 2] << 8 | data[at + 3];
        const unsigned char *value = data + at + 4;
        if ((type == 0x0020 || type == 0x0001) && len >= 8 && value[1] == 0x01 && at + 4 + 8 <= size) {
            bool          x = type == 0x0020;
            int           port = (value[2] << 8 | value[3]) ^ (x ? kStunCookie >> 16 : 0);
            unsigned char ip[4];
            for (int i = 0; i < 4; i++) {
                ip[i] = value[4 + i] ^ (x ? (unsigned char) (kStunCookie >> (24 - 8 * i)) : 0);
            }
            std::snprintf(g_wan, sizeof g_wan, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
            g_wan_port = port;
            g_stun_state = 2;
            std::fprintf(stderr, "netplay: internet address %s:%d (STUN; this PC listens on %d)%s\n", g_wan, port, g_local_port,
                         NetSocketWanLikelyCgnat() ? ": likely carrier-grade or strict NAT, so joining by address will not work; use a Steam friend" : "");
            return true;
        }
        at += 4 + ((len + 3) & ~3);
    }
    return true;
}

} // namespace

void NetSocketStunStart(const char *server, int local_port) {
    if (g_stun_state != 0 || g_socket == kNoSocket || server == nullptr || std::strcmp(server, "off") == 0) {
        return;
    }
    g_stun_state = 1;
    g_local_port = local_port;
    std::string spec = server;
    g_stun_thread = std::thread([spec] {
        std::size_t colon = spec.rfind(':');
        std::string host = colon == std::string::npos ? spec : spec.substr(0, colon);
        int         port = colon == std::string::npos ? 3478 : std::atoi(spec.c_str() + colon + 1);
        std::uint32_t address = 0;
        if (!Resolve(host, address)) {
            g_stun_state = 3;
            std::fprintf(stderr, "netplay: internet address: cannot find %s\n", host.c_str());
            return;
        }
        g_stun_server.family = 2;
        g_stun_server.port = NetOrder((std::uint16_t) port);
        g_stun_server.address = address;
        std::mt19937 random((unsigned) std::chrono::steady_clock::now().time_since_epoch().count());
        for (unsigned char &b : g_stun_id) {
            b = (unsigned char) random();
        }
        unsigned char request[20] = {0x00, 0x01, 0x00, 0x00, 0x21, 0x12, 0xA4, 0x42};
        std::memcpy(request + 8, g_stun_id, 12);
        for (int i = 0; i < 3 && g_stun_state == 1 && !g_stun_stop.load(); i++) {
            sendto(g_socket, (const char *) request, sizeof request, 0, ADDRESS(&g_stun_server), sizeof g_stun_server);
            StunWait(700);
        }
        StunWait(1500);
        int asking = 1;
        if (!g_stun_stop.load() && g_stun_state.compare_exchange_strong(asking, 3)) {
            std::fprintf(stderr, "netplay: internet address: no STUN answer from %s\n", spec.c_str());
        }
    });
}

// coop-cand58: the STUN asker stopped and joined as the game ends (NetplayStopThreads): its waits end at once. True when it ran.
bool NetSocketStunStop() {
    if (!g_stun_thread.joinable()) {
        return false;
    }
    g_stun_stop = true;
    g_stun_thread.join();
    return true;
}

const char *NetSocketWan(int &port) {
    port = g_wan_port;
    return g_stun_state == 2 ? g_wan : "";
}

bool NetSocketWanLikelyCgnat() {
    unsigned a = 0, b = 0;
    std::sscanf(g_wan, "%u.%u", &a, &b);
    return g_stun_state == 2 && ((a == 100 && b >= 64 && b < 128) || (g_local_port != 0 && g_wan_port != g_local_port));
}

int NetSocketReceive(void *data, std::size_t size) {
    if (g_impair.on) {
        std::lock_guard<std::mutex> hold(g_send_lock);

        Release();
    }

    for (;;) {

#if defined(_WIN32)
    int from_size = sizeof g_from;
#else
    socklen_t from_size = sizeof g_from;
#endif
#if defined(_WIN32)
    int got = (int) recvfrom(g_socket, (char *) data, (int) size, 0, (void *) &g_from, &from_size);
#else
    int got = (int) recvfrom(g_socket, (char *) data, size, 0, reinterpret_cast<sockaddr *>(&g_from), &from_size);
#endif
    if (got > 0 && g_stun_state != 0 && g_from.address == g_stun_server.address && TakeStun((const unsigned char *) data, got)) {
        continue; // the STUN server's answer, not the other copy's
    }
    static const bool blocked = TestEnvInt("DC_NETPLAY_TEST_NAT_BLOCK", 0) != 0;
    static bool said = false;
    // coop-steamsplit (mp-nat -OldGuest): the block said as the socket is first read, not at its first datagram dropped: a
    // copy nobody punched (the old guest went by Steam at once) never said it, though its "router" was shut all along.
    if (blocked && !said) {
        said = true;
        std::fprintf(stderr, "netplay: test: NAT block: every datagram but STUN's dropped\n");
    }
    if (got > 0 && (blocked || g_test_cut)) {
        if (!said) {
            said = true;
            std::fprintf(stderr, "netplay: test: NAT block: every datagram but STUN's dropped\n");
        }
        continue;
    }
    if (got > 0) {
        g_udp_heard++;
        g_udp_from = g_from;
        if (std::find(g_sent_to.begin(), g_sent_to.end(), std::make_pair((std::uint32_t) g_from.address, (std::uint16_t) g_from.port)) == g_sent_to.end() ||
            NetplayMutated("port_proof_any")) {
            g_udp_fresh++;
            g_udp_fresh_from = g_from;
        }
    }
    if (got == 4 && std::memcmp(data, kPunch, 4) == 0) {
        continue; // a NAT punch (NetSocketPunch): it only opens the way
    }
    if (got > 0) {
        g_from_steam = 0;
        return got;
    }
    break;
    }

    // Nothing by UDP: anything over Steam (once it is up: a Steam host, or a guest joined through it).
    if (NetSteamSelf() != 0) {
        std::uint64_t from = 0;

        int got = NetSteamReceive(data, size, from);

        // A NAT code a Steam guest sends (netplay.cpp, Host > NAT > Steam): where to punch, not link data.
        if (got > 6 && std::memcmp(data, "DCNAT ", 6) == 0) {
            g_nat_heard.assign(static_cast<const char *>(data) + 6, (std::size_t) got - 6);
            return 0;
        }
        // coop-natonly: Steam carries the codes only (NetSocketSteamCodesOnly): anything else over it is dropped, so a
        // guest that came over Steam is never answered (no Hello back, no one-sided link).
        if (got > 0 && g_steam_codes_only) {
            static bool said = false;

            if (!said) {
                said = true;
                std::fprintf(stderr, "netplay: nat: a guest came over Steam (steam:%llu): not taken, NAT > Steam plays direct only\n",
                             (unsigned long long) from);
            }
            return 0;
        }
        if (got > 0) {
            std::memset(&g_from, 0, sizeof g_from);
            g_from_steam = from;
            return got;
        }
    }

    return 0;
}

bool NetSocketValidAddress(const char *text) {
    std::uint32_t address = 0;

    return text != nullptr && ParseAddress(text, address) && address != 0;
}

const char *NetSocketLocalAddress() {
    static char text[24] = "";

    if (text[0] != '\0') {
        return text;
    }

    constexpr int kInet = 2, kDatagram = 2, kUdp = 17;
#if defined(_WIN32)
    alignas(8) unsigned char wsa[1024];

    if (WSAStartup(0x0202, wsa) != 0) {
        return text;
    }

    std::uintptr_t probe = socket(kInet, kDatagram, kUdp);
#else
    int probe = socket(kInet, kDatagram, kUdp);
#endif

    if (probe == kNoSocket) {
        return text;
    }

    // A datagram socket "connected" to an outside address sends nothing; the system only picks the
    // interface its route leaves by, whose address getsockname then gives (the LAN one, not WSL's).
    Address outside{};
    Address local{};

    outside.family = kInet;
    outside.port = NetOrder(53);
    ParseAddress("8.8.8.8", outside.address);
#if defined(_WIN32)
    int size = sizeof local;
    bool ok = connect(probe, ADDRESS(&outside), sizeof outside) == 0 && getsockname(probe, &local, &size) == 0;
    closesocket(probe);
#else
    socklen_t size = sizeof local;
    bool ok = connect(probe, ADDRESS(&outside), sizeof outside) == 0 && getsockname(probe, reinterpret_cast<sockaddr *>(&local), &size) == 0;
    close(probe);
#endif

    if (ok && local.address != 0) {
        const auto *bytes = reinterpret_cast<const unsigned char *>(&local.address);

        std::snprintf(text, sizeof text, "%u.%u.%u.%u", bytes[0], bytes[1], bytes[2], bytes[3]);
    }

    return text;
}
void NetSocketPunch(const char *address, int port) {
    Address to{};

    if (g_socket == kNoSocket || !ParseAddress(address, to.address)) {
        return;
    }
    to.family = 2;
    to.port = NetOrder((std::uint16_t) port);
    sendto(g_socket, kPunch, 4, 0, ADDRESS(&to), sizeof to);
    NoteSentTo(to);
    g_udp_sent++;
}
void NetSocketUdpCounts(long long &sent, long long &heard, const char *&last_from) {
    static char text[24];
    const auto *bytes = (const unsigned char *) &g_udp_from.address;

    std::snprintf(text, sizeof text, "%u.%u.%u.%u:%u", bytes[0], bytes[1], bytes[2], bytes[3], (unsigned) NetOrder(g_udp_from.port));
    sent = g_udp_sent;
    heard = g_udp_heard;
    last_from = text;
}
void NetSocketUdpFresh(long long &fresh, const char *&from) {
    static char text[24];
    const auto *bytes = (const unsigned char *) &g_udp_fresh_from.address;

    std::snprintf(text, sizeof text, "%u.%u.%u.%u:%u", bytes[0], bytes[1], bytes[2], bytes[3], (unsigned) NetOrder(g_udp_fresh_from.port));
    fresh = g_udp_fresh;
    from = text;
}
void NetSocketTestCut() {
    g_test_cut = true;
}
void NetSocketSteamCodesOnly(bool on) {
    g_steam_codes_only = on;
}
const char *NetSocketNatHeard() {
    return g_nat_heard.c_str();
}

namespace {
// A NAT code's host on this PC's own network (its internet address is this PC's): the LAN port its Hellos go to by
// broadcast as well, 0 when not looking.
int g_lan_port = 0;
} // namespace

void NetSocketLanSearch(int port) {
    if (g_socket == kNoSocket || port <= 0) {
        g_lan_port = 0;
        return;
    }
    int on = 1;
#if defined(_WIN32)
    const int ok = setsockopt(g_socket, 0xFFFF, 0x0020, (const char *) &on, sizeof on); // SOL_SOCKET, SO_BROADCAST
#else
    const int ok = setsockopt(g_socket, SOL_SOCKET, SO_BROADCAST, &on, sizeof on);
#endif
    g_lan_port = ok == 0 ? port : 0;
    if (ok != 0) {
        std::fprintf(stderr, "netplay: nat: cannot broadcast on the LAN\n");
    }
}

void NetSocketLanBroadcast(const void *data, std::size_t size) {
    Address to{};

    if (g_lan_port == 0 || g_socket == kNoSocket) {
        return;
    }
    to.family = 2;
    to.port = NetOrder((std::uint16_t) g_lan_port);
    to.address = 0xFFFFFFFFu; // 255.255.255.255
    sendto(g_socket, (const char *) data, (int) size, 0, ADDRESS(&to), sizeof to);
}

const char *NetSocketTakeLanSender() {
    if (g_lan_port == 0 || g_kept.address == 0 || g_kept_steam != 0) {
        return nullptr;
    }
    g_lan_port = 0;
    if (g_kept.address == g_peer.address && g_kept.port == g_peer.port) {
        return nullptr; // the punch got through after all
    }
    g_peer = g_kept;

    const auto *bytes = (const unsigned char *) &g_peer.address;
    std::snprintf(g_peer_text, sizeof g_peer_text, "%u.%u.%u.%u:%u", bytes[0], bytes[1], bytes[2], bytes[3], (unsigned) NetOrder(g_peer.port));
    return g_peer_text;
}
