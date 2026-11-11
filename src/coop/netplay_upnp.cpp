// No game header here, as in netplay_socket.cpp: on Windows the system's socket headers fail under the game's types.h,
// so the Winsock calls UPnP needs are declared by hand.
#include "coop/game_ns.hpp"
#include "coop/netplay_upnp.hpp"

#include "coop/coop.hpp"
#include "coop/netplay_socket.hpp"
#include "coop/netplay_test.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
extern "C" {
__declspec(dllimport) int __stdcall WSAStartup(unsigned short version, void *data);
__declspec(dllimport) std::uintptr_t __stdcall socket(int family, int type, int protocol);
__declspec(dllimport) int __stdcall sendto(std::uintptr_t socket, const char *data, int size, int flags, const void *to, int to_size);
__declspec(dllimport) int __stdcall recvfrom(std::uintptr_t socket, char *data, int size, int flags, void *from, int *from_size);
__declspec(dllimport) int __stdcall connect(std::uintptr_t socket, const void *address, int size);
__declspec(dllimport) int __stdcall send(std::uintptr_t socket, const char *data, int size, int flags);
__declspec(dllimport) int __stdcall recv(std::uintptr_t socket, char *data, int size, int flags);
__declspec(dllimport) int __stdcall getsockname(std::uintptr_t socket, void *address, int *size);
__declspec(dllimport) int __stdcall closesocket(std::uintptr_t socket);
__declspec(dllimport) int __stdcall setsockopt(std::uintptr_t socket, int level, int name, const char *value, int size);
}
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace {

#if defined(_WIN32)
using Socket = std::uintptr_t;
constexpr Socket kNoSocket = ~std::uintptr_t{0};
#else
using Socket = int;
constexpr Socket kNoSocket = -1;
#endif

struct Address {
    std::uint16_t family;
    std::uint16_t port;
    std::uint32_t address;
    std::uint8_t  zero[8];
};

static_assert(sizeof(Address) == 16);

std::uint16_t NetOrder(std::uint16_t value) {
    return (std::uint16_t) ((value >> 8) | (value << 8));
}

bool ParseIp(const std::string &text, std::uint32_t &out) {
    unsigned parts[4];
    char     tail = 0;

    if (std::sscanf(text.c_str(), "%u.%u.%u.%u%c", &parts[0], &parts[1], &parts[2], &parts[3], &tail) != 4) {
        return false;
    }
    unsigned char bytes[4];
    for (int i = 0; i < 4; i++) {
        if (parts[i] > 255) {
            return false;
        }
        bytes[i] = (unsigned char) parts[i];
    }
    std::memcpy(&out, bytes, 4);
    return true;
}

std::string IpText(std::uint32_t address) {
    const auto *b = reinterpret_cast<const unsigned char *>(&address);
    char        text[20];

    std::snprintf(text, sizeof text, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    return text;
}

void CloseSocket(Socket s) {
#if defined(_WIN32)
    closesocket(s);
#else
    close(s);
#endif
}

// A blocking socket's send and receive give up after ms.
void SetTimeouts(Socket s, int ms) {
#if defined(_WIN32)
    const unsigned long value = (unsigned long) ms;
    setsockopt(s, 0xFFFF, 0x1006, (const char *) &value, sizeof value); // SOL_SOCKET, SO_RCVTIMEO
    setsockopt(s, 0xFFFF, 0x1005, (const char *) &value, sizeof value); // SO_SNDTIMEO
#else
    timeval value{ms / 1000, (ms % 1000) * 1000};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &value, sizeof value);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &value, sizeof value);
#endif
}

bool StartSockets() {
#if defined(_WIN32)
    alignas(8) static unsigned char wsa[1024];
    return WSAStartup(0x0202, wsa) == 0;
#else
    return true;
#endif
}

// "http://a.b.c.d[:port]/path" into its parts (an address only: routers name themselves by it).
struct Url {
    std::string host;
    int         port = 80;
    std::string path = "/";
};

bool ParseUrl(const std::string &text, Url &out) {
    if (text.rfind("http://", 0) != 0) {
        return false;
    }
    std::string rest = text.substr(7);
    std::size_t slash = rest.find('/');
    std::string where = rest.substr(0, slash);
    out.path = slash == std::string::npos ? "/" : rest.substr(slash);
    std::size_t colon = where.find(':');
    out.host = where.substr(0, colon);
    out.port = colon == std::string::npos ? 80 : std::atoi(where.c_str() + colon + 1);
    std::uint32_t ip = 0;
    return out.port > 0 && out.port < 65536 && ParseIp(out.host, ip);
}

// A control URL against the description's base: absolute, from the root, or beside it.
Url Resolve(const Url &base, const std::string &link) {
    Url out;
    if (ParseUrl(link, out)) {
        return out;
    }
    out = base;
    out.path = !link.empty() && link[0] == '/' ? link : "/" + link;
    return out;
}

// The text between <tag> and </tag> from at on (no namespace prefixes: IGDs write plain ones), "" when none.
std::string Tag(const std::string &xml, const char *tag, std::size_t from = 0, std::size_t *end = nullptr) {
    const std::string open = std::string("<") + tag + ">", close = std::string("</") + tag + ">";
    std::size_t       at = xml.find(open, from);
    if (at == std::string::npos) {
        return "";
    }
    at += open.size();
    std::size_t stop = xml.find(close, at);
    if (stop == std::string::npos) {
        return "";
    }
    if (end != nullptr) {
        *end = stop + close.size();
    }
    std::string value = xml.substr(at, stop - at);
    while (!value.empty() && (value.front() == ' ' || value.front() == '\n' || value.front() == '\r' || value.front() == '\t')) {
        value.erase(value.begin());
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\n' || value.back() == '\r' || value.back() == '\t')) {
        value.pop_back();
    }
    return value;
}

// A header's value from an HTTP or SSDP answer (case ignored), "" when it has none.
std::string Header(const std::string &text, const char *name) {
    const std::size_t length = std::strlen(name);

    for (std::size_t at = 0; at < text.size();) {
        std::size_t eol = text.find("\r\n", at);
        if (eol == std::string::npos) {
            eol = text.size();
        }
        if (eol == at) {
            break;
        }
        if (eol - at > length && text[at + length] == ':') {
            bool same = true;
            for (std::size_t i = 0; i < length && same; i++) {
                same = std::tolower((unsigned char) text[at + i]) == std::tolower((unsigned char) name[i]);
            }
            if (same) {
                std::string value = text.substr(at + length + 1, eol - at - length - 1);
                while (!value.empty() && value.front() == ' ') {
                    value.erase(value.begin());
                }
                return value;
            }
        }
        at = eol + 2;
    }
    return "";
}

// One HTTP exchange (Connection: close, so the answer ends when the router closes): status and body; local gets the
// address this PC reached the router from (the mapping's internal client). 0 when it could not.
int Http(const Url &url, const std::string &request, std::string &body, std::string *local = nullptr) {
    constexpr int kInet = 2, kStream = 1, kTcp = 6;
    Address       to{};

    body.clear();
    if (!ParseIp(url.host, to.address)) {
        return 0;
    }
    to.family = kInet;
    to.port = NetOrder((std::uint16_t) url.port);

    Socket s = socket(kInet, kStream, kTcp);
    if (s == kNoSocket) {
        return 0;
    }
    SetTimeouts(s, 3000);
#if defined(_WIN32)
    const bool linked = connect(s, &to, sizeof to) == 0;
#else
    const bool linked = connect(s, reinterpret_cast<const sockaddr *>(&to), sizeof to) == 0;
#endif
    if (!linked) {
        CloseSocket(s);
        return 0;
    }
    if (local != nullptr) {
        Address here{};
#if defined(_WIN32)
        int size = sizeof here;
        getsockname(s, &here, &size);
#else
        socklen_t size = sizeof here;
        getsockname(s, reinterpret_cast<sockaddr *>(&here), &size);
#endif
        *local = IpText(here.address);
    }

    for (std::size_t sent = 0; sent < request.size();) {
        const int n = (int) send(s, request.data() + sent, (int) (request.size() - sent), 0);
        if (n <= 0) {
            CloseSocket(s);
            return 0;
        }
        sent += (std::size_t) n;
    }
    std::string answer;
    char        chunk[4096];
    for (;;) {
        const int n = (int) recv(s, chunk, sizeof chunk, 0);
        if (n <= 0 || answer.size() > (1u << 20)) {
            break;
        }
        answer.append(chunk, (std::size_t) n);
    }
    CloseSocket(s);

    int status = 0;
    if (std::sscanf(answer.c_str(), "HTTP/%*d.%*d %d", &status) != 1) {
        return 0;
    }
    const std::size_t head = answer.find("\r\n\r\n");
    std::string       raw = head == std::string::npos ? "" : answer.substr(head + 4);
    std::string       encoding = Header(answer.substr(answer.find("\r\n") + 2), "Transfer-Encoding");

    if (encoding.find("chunked") != std::string::npos) {
        for (std::size_t at = 0; at < raw.size();) {
            const std::size_t eol = raw.find("\r\n", at);
            if (eol == std::string::npos) {
                break;
            }
            const unsigned long size = std::strtoul(raw.c_str() + at, nullptr, 16);
            if (size == 0 || eol + 2 + size > raw.size()) {
                break;
            }
            body.append(raw, eol + 2, size);
            at = eol + 2 + size + 2;
        }
    } else {
        body = raw;
    }
    return status;
}

// The router found by SSDP: its WAN connection service's control URL and type.
struct Gateway {
    Url         control;
    std::string service;
    std::string name; // its model, for the log
};

// Asks every IGD (or the test's mock) where its description is, then reads it for a WAN connection service.
bool Discover(Gateway &out, std::string &why) {
    constexpr int kInet = 2, kDatagram = 2, kUdp = 17;
    const char   *mock = TestEnv("DC_NETPLAY_UPNP_SSDP");
    Address       to{};
    std::string   host = "239.255.255.250";
    int           port = 1900;

    if (mock != nullptr) {
        const std::string text = mock;
        const std::size_t colon = text.rfind(':');
        host = text.substr(0, colon);
        port = colon == std::string::npos ? 1900 : std::atoi(text.c_str() + colon + 1);
    }
    to.family = kInet;
    to.port = NetOrder((std::uint16_t) port);
    ParseIp(host, to.address);

    Socket s = socket(kInet, kDatagram, kUdp);
    if (s == kNoSocket) {
        why = "no socket for SSDP";
        return false;
    }
    SetTimeouts(s, 250);
    // Out of the interface the default route leaves by (a PC with Tailscale, WSL or a VPN has others), two hops at most.
    const int ttl = 2;
#if defined(_WIN32)
    setsockopt(s, 0, 10, (const char *) &ttl, sizeof ttl); // IPPROTO_IP, IP_MULTICAST_TTL
#else
    setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof ttl);
#endif
    std::uint32_t here = 0;
    if (mock == nullptr && ParseIp(NetSocketLocalAddress(), here)) {
#if defined(_WIN32)
        setsockopt(s, 0, 9, (const char *) &here, sizeof here); // IP_MULTICAST_IF
#else
        setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF, &here, sizeof here);
#endif
    }

    static const char *const kTargets[] = {"urn:schemas-upnp-org:device:InternetGatewayDevice:1",
                                           "urn:schemas-upnp-org:device:InternetGatewayDevice:2",
                                           "urn:schemas-upnp-org:service:WANIPConnection:1"};
    std::vector<std::string> tried;
    int                      answers = 0;

    for (int round = 0; round < 3; round++) {
        for (const char *target : kTargets) {
            char request[256];
            const int size = std::snprintf(request, sizeof request,
                                           "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\nST: %s\r\n\r\n", target);
#if defined(_WIN32)
            sendto(s, request, size, 0, &to, sizeof to);
#else
            sendto(s, request, (std::size_t) size, 0, reinterpret_cast<const sockaddr *>(&to), sizeof to);
#endif
        }
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(round == 0 ? 2500 : 1500);
        while (std::chrono::steady_clock::now() < until) {
            char    answer[2048];
            Address from{};
#if defined(_WIN32)
            int from_size = sizeof from;
            const int got = recvfrom(s, answer, sizeof answer - 1, 0, &from, &from_size);
#else
            socklen_t from_size = sizeof from;
            const int got = (int) recvfrom(s, answer, sizeof answer - 1, 0, reinterpret_cast<sockaddr *>(&from), &from_size);
#endif
            if (got <= 0) {
                continue;
            }
            answers++;
            const std::string text(answer, (std::size_t) got);
            const std::string location = Header(text.substr(text.find("\r\n") == std::string::npos ? 0 : text.find("\r\n") + 2), "LOCATION");
            Url               url;

            if (location.empty() || !ParseUrl(location, url)) {
                continue;
            }
            bool seen = false;
            for (const std::string &t : tried) {
                seen = seen || t == location;
            }
            if (seen) {
                continue;
            }
            tried.push_back(location);

            std::string xml;
            const std::string get = "GET " + url.path + " HTTP/1.1\r\nHost: " + url.host + ":" + std::to_string(url.port) +
                                    "\r\nConnection: close\r\n\r\n";
            if (Http(url, get, xml) != 200) {
                std::fprintf(stderr, "netplay: upnp: %s gave no description\n", location.c_str());
                continue;
            }
            // The first WANIPConnection (any version), else a WANPPPConnection.
            std::string service, control;
            for (int pass = 0; pass < 2 && control.empty(); pass++) {
                for (std::size_t at = 0; control.empty();) {
                    std::size_t end = 0;
                    const std::string block = Tag(xml, "service", at, &end);
                    if (end == 0) {
                        break;
                    }
                    at = end;
                    const std::string type = Tag(block, "serviceType");
                    if (type.find(pass == 0 ? ":WANIPConnection:" : ":WANPPPConnection:") != std::string::npos) {
                        service = type;
                        control = Tag(block, "controlURL");
                    }
                }
            }
            if (control.empty()) {
                std::fprintf(stderr, "netplay: upnp: %s is not an internet gateway (no WAN connection service)\n", location.c_str());
                continue;
            }
            const std::string base = Tag(xml, "URLBase");
            Url               base_url = url;
            if (!base.empty()) {
                ParseUrl(base, base_url);
            }
            out.control = Resolve(base_url, control);
            out.service = service;
            out.name = Tag(xml, "modelName");
            if (out.name.empty()) {
                out.name = Tag(xml, "friendlyName");
            }
            CloseSocket(s);
            return true;
        }
    }
    CloseSocket(s);
    why = answers == 0 ? "no router answered (UPnP is off on the router, or the firewall dropped its answer)"
                       : "the routers that answered had no WAN connection service";
    return false;
}

// One SOAP action: HTTP status (200 done), the body, and the UPnP error code of a refusal (0 when none).
int Soap(const Gateway &gateway, const char *action, const std::string &arguments, std::string &body, int &error,
         std::string *local = nullptr) {
    const std::string envelope =
        "<?xml version=\"1.0\"?>\r\n<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:" +
        std::string(action) + " xmlns:u=\"" + gateway.service + "\">" + arguments + "</u:" + action + "></s:Body></s:Envelope>\r\n";
    const std::string request = "POST " + gateway.control.path + " HTTP/1.1\r\nHost: " + gateway.control.host + ":" +
                                std::to_string(gateway.control.port) +
                                "\r\nContent-Type: text/xml; charset=\"utf-8\"\r\nSOAPAction: \"" + gateway.service + "#" + action +
                                "\"\r\nContent-Length: " + std::to_string(envelope.size()) + "\r\nConnection: close\r\n\r\n" + envelope;
    const int status = Http(gateway.control, request, body, local);
    const std::string code = Tag(body, "errorCode");

    error = code.empty() ? 0 : std::atoi(code.c_str());
    return status;
}

const char *ErrorText(int error) {
    switch (error) {
    case 402: return "invalid arguments";
    case 501: return "action failed";
    case 606: return "not authorized (the router allows UPnP reads only)";
    case 714: return "no such mapping";
    case 715: return "wildcards not allowed for the remote host";
    case 716: return "wildcards not allowed for the outside port";
    case 718: return "the outside port is taken by another mapping";
    case 724: return "the router wants the same port inside and out";
    case 725: return "the router takes only permanent leases";
    case 726: return "the remote host must be a wildcard";
    case 727: return "the outside port must be a wildcard";
    case 728: return "the router's mapping table is full";
    default:  return "refused";
    }
}

// The mapping, shared between the worker thread, the game (NetUpnpOutside) and the exit's removal.
std::mutex          g_lock;           // held around every exchange with the router and the fields below
std::atomic<bool>   g_started{false};
std::atomic<bool>   g_stopping{false};
std::atomic<bool>   g_mapped{false};    // ours on the router, to renew and remove
std::atomic<int>    g_state{kUpnpIdle};
Gateway             g_gateway;
std::string         g_internal;       // this PC's address on the router's side
std::string         g_outside;        // the router's outside address
int                 g_port = 0;       // this PC's port
int                 g_outside_port = 0;
int                 g_lease = 0;      // seconds; 0 permanent
char                g_outside_text[24] = "";
char                g_why[96] = "";   // a failure, short, for the waiting line

std::string MappingArguments(int outside_port, int lease) {
    return "<NewRemoteHost></NewRemoteHost><NewExternalPort>" + std::to_string(outside_port) +
           "</NewExternalPort><NewProtocol>UDP</NewProtocol><NewInternalPort>" + std::to_string(g_port) +
           "</NewInternalPort><NewInternalClient>" + g_internal +
           "</NewInternalClient><NewEnabled>1</NewEnabled><NewPortMappingDescription>Dark Cloud co-op</NewPortMappingDescription>"
           "<NewLeaseDuration>" + std::to_string(lease) + "</NewLeaseDuration>";
}

std::string DeleteArguments(int outside_port) {
    return "<NewRemoteHost></NewRemoteHost><NewExternalPort>" + std::to_string(outside_port) +
           "</NewExternalPort><NewProtocol>UDP</NewProtocol>";
}

// AddPortMapping for outside_port (g_lock held): its UPnP error, 0 done, -1 no answer.
int Add(int outside_port, int lease) {
    std::string body;
    int         error = 0;
    const int   status = Soap(g_gateway, "AddPortMapping", MappingArguments(outside_port, lease), body, error);

    return status == 200 ? 0 : status == 0 ? -1 : error != 0 ? error : status;
}

// Who the router's mapping of outside_port is for: 1 this game's own (a copy that crashed before removing it), 2 this PC's
// port but not made by the game (forwarded by hand, or another program), 0 none, someone else, or not said.
int Owner(int outside_port) {
    std::string body;
    int         error = 0;
    const std::string arguments = "<NewRemoteHost></NewRemoteHost><NewExternalPort>" + std::to_string(outside_port) +
                                  "</NewExternalPort><NewProtocol>UDP</NewProtocol>";

    if (Soap(g_gateway, "GetSpecificPortMappingEntry", arguments, body, error) != 200 || Tag(body, "NewInternalClient") != g_internal ||
        std::atoi(Tag(body, "NewInternalPort").c_str()) != g_port) {
        return 0;
    }
    return Tag(body, "NewPortMappingDescription") == "Dark Cloud co-op" ? 1 : 2;
}

void Fail(const char *why) {
    std::snprintf(g_why, sizeof g_why, "%s", why);
    g_state = kUpnpFailed;
}

bool Private(const std::string &address) {
    unsigned a = 0, b = 0;
    if (std::sscanf(address.c_str(), "%u.%u", &a, &b) != 2) {
        return true;
    }
    return a == 10 || a == 127 || a == 0 || (a == 172 && b >= 16 && b < 32) || (a == 192 && b == 168) || (a == 100 && b >= 64 && b < 128) ||
           (a == 169 && b == 254);
}

void Work() {
    StartSockets();

    Gateway     gateway;
    std::string why;
    const auto  t0 = std::chrono::steady_clock::now();
    auto        ms = [&] { return (long long) std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count(); };

    std::fprintf(stderr, "netplay: upnp: looking for the router (UDP %d)\n", g_port);
    if (!Discover(gateway, why)) {
        std::fprintf(stderr, "netplay: upnp: failed: %s; forward UDP %d by hand (if not done already), or use Tailscale or a Steam friend\n",
                     why.c_str(), g_port);
        Fail("no UPnP router");
        return;
    }

    std::unique_lock<std::mutex> hold(g_lock);
    if (g_stopping) {
        return;
    }
    g_gateway = gateway;

    // The outside address, and this PC's address as the router sees it (the connection's own end).
    std::string body;
    int         error = 0;
    if (Soap(g_gateway, "GetExternalIPAddress", "", body, error, &g_internal) != 200) {
        std::fprintf(stderr, "netplay: upnp: failed: %s (%s) did not give its outside address (error %d)\n", g_gateway.control.host.c_str(),
                     g_gateway.name.c_str(), error);
        Fail("the router did not answer UPnP");
        return;
    }
    g_outside = Tag(body, "NewExternalIPAddress");
    std::snprintf(g_outside_text, sizeof g_outside_text, "%s", g_outside.c_str());

    // Already forwarded to this PC, by hand (Jep, 2026-10-28: Host > IP handles a port forwarded entirely as well as UPnP):
    // left as it is, never renewed or removed. A mapping of the game's own from a crashed copy is made again below.
    const int owner = Owner(g_port);
    if (owner == 2 && !NetplayMutated("upnp_ignore_forward")) {
        g_outside_port = g_port;
        g_state = kUpnpForwarded;
        std::fprintf(stderr, "netplay: upnp: UDP %d already forwarded to this PC (%s:%d) on %s (%s): left as it is, outside address %s\n", g_port,
                     g_internal.c_str(), g_port, g_gateway.control.host.c_str(), g_gateway.name.empty() ? "unnamed" : g_gateway.name.c_str(),
                     g_outside.c_str());
        return;
    }
    if (owner == 1) {
        Soap(g_gateway, "DeletePortMapping", DeleteArguments(g_port), body, error);
        std::fprintf(stderr, "netplay: upnp: an old mapping of this game's on UDP %d (a copy that did not quit cleanly): made again\n", g_port);
    }

    int lease = TestEnvInt("DC_NETPLAY_UPNP_LEASE_S", 3600);
    int outside_port = 0;
    int result = -1;
    // The game's port outside too; another PC's mapping on it (718): ten, twenty ... above, so a NAT code stays short.
    for (int k = 0; k < 5; k++) {
        const int want = g_port + 10 * k;

        result = Add(want, lease);
        if (result == 725 && lease != 0) {
            lease = 0;
            result = Add(want, lease);
        }
        if (result == 718 && Owner(want) == 1) {
            Soap(g_gateway, "DeletePortMapping", DeleteArguments(want), body, error);
            result = Add(want, lease);
        }
        if (result == 0) {
            outside_port = want;
            break;
        }
        if (result != 718) {
            break;
        }
    }
    if (result != 0) {
        std::fprintf(stderr, "netplay: upnp: failed: %s (%s) refused UDP %d: %s (error %d)\n", g_gateway.control.host.c_str(),
                     g_gateway.name.c_str(), g_port, result == -1 ? "no answer" : ErrorText(result), result);
        Fail("the router refused UPnP");
        return;
    }
    g_outside_port = outside_port;
    g_lease = lease;
    g_mapped = true;
    g_state = kUpnpMapped;
    std::fprintf(stderr, "netplay: upnp: mapped %s:%d -> %s:%d via %s (%s, %s), lease %d s, %lld ms\n", g_outside.c_str(), outside_port,
                 g_internal.c_str(), g_port, g_gateway.control.host.c_str(), g_gateway.name.empty() ? "unnamed" : g_gateway.name.c_str(),
                 g_gateway.service.c_str(), lease, ms());
    if (Private(g_outside)) {
        std::fprintf(stderr, "netplay: upnp: the router's outside address %s is not a public one: it sits behind another router or the "
                             "provider's NAT (CGNAT), so the mapping does not reach the internet; use Tailscale or a Steam friend\n",
                     g_outside.c_str());
    }

    // Renewed at half the lease (a permanent one re-added every half hour, in case the router restarted).
    for (int renewal = 1;; renewal++) {
        const int  every = lease > 0 ? std::max(1, lease / 2) : 1800;
        const auto due = std::chrono::steady_clock::now() + std::chrono::seconds(every);

        hold.unlock();
        while (std::chrono::steady_clock::now() < due && !g_stopping) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        hold.lock();
        if (g_stopping || !g_mapped) {
            return;
        }
        if (NetplayMutated("upnp_no_renew")) {
            // coop-fx51f: the twin says each renewal it skips (a test line), so its run ends on the moment, not a wait
            std::fprintf(stderr, "netplay: upnp: test: renewal %d skipped (upnp_no_renew)\n", renewal);
            continue;
        }
        const int again = Add(outside_port, lease);
        if (again == 0) {
            std::fprintf(stderr, "netplay: upnp: renewed %s:%d -> %s:%d (renewal %d, lease %d s)\n", g_outside.c_str(), outside_port, g_internal.c_str(),
                         g_port, renewal, lease);
        } else {
            std::fprintf(stderr, "netplay: upnp: renewal %d refused: %s (error %d); trying again in %d s\n", renewal,
                         again == -1 ? "no answer" : ErrorText(again), again, every);
        }
    }
}

} // namespace

void NetUpnpStart(int port) {
    if (g_started.exchange(true)) {
        return;
    }
    g_port = port;
    g_state = kUpnpTrying;
    std::atexit(NetUpnpStop);
    std::thread(Work).detach();
}

const char *NetUpnpOutside(int &port) {
    const bool open = g_state == kUpnpMapped || g_state == kUpnpForwarded;

    port = open ? g_outside_port : 0;
    return open ? g_outside_text : "";
}

bool NetUpnpPublic() {
    return (g_state == kUpnpMapped || g_state == kUpnpForwarded) && !Private(g_outside_text);
}

int NetUpnpState() {
    return g_state;
}

const char *NetUpnpWhy() {
    return g_state == kUpnpFailed ? g_why : "";
}

void NetUpnpStop() {
    g_stopping = true;
    // (Not waiting long on a worker that is mid-exchange: the lease ends a crashed copy's mapping anyway.)
    std::unique_lock<std::mutex> hold(g_lock, std::defer_lock);
    for (int i = 0; i < 40 && !hold.try_lock(); i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (!hold.owns_lock() || !g_mapped.exchange(false)) {
        return;
    }
    g_state = kUpnpIdle;
    if (NetplayMutated("upnp_no_remove")) {
        std::fprintf(stderr, "netplay: upnp: test: leaving %s:%d on the router\n", g_outside.c_str(), g_outside_port);
        return;
    }
    std::string body;
    int         error = 0;
    const int   status = Soap(g_gateway, "DeletePortMapping", DeleteArguments(g_outside_port), body, error);

    if (status == 200) {
        std::fprintf(stderr, "netplay: upnp: removed %s:%d from %s\n", g_outside.c_str(), g_outside_port, g_gateway.control.host.c_str());
    } else {
        std::fprintf(stderr, "netplay: upnp: removing %s:%d failed: %s (error %d); its lease ends it in %d s\n", g_outside.c_str(), g_outside_port,
                     status == 0 ? "no answer" : ErrorText(error), error, g_lease);
    }
    std::fflush(stderr);
}
