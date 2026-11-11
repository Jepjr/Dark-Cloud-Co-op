#pragma once

#include "coop/game_ns.hpp"

#include <cstddef>

// The co-op link's UDP socket, apart from the game's headers, which clash with the system's socket
// headers on Windows (RECT among them). One socket, non-blocking, sending to one peer.

// Listens on listen_port and aims at peer:peer_port. An empty peer (or "any") is learnt instead:
// NetSocketAnswerHello aims at whoever sent the last Hello. False, with a line on stderr, when it
// cannot.
bool NetSocketOpen(int listen_port, const char *peer, int peer_port);

// Whether the peer was left to be learnt, and if so aims at the sender of the last datagram that
// NetSocketKeepSender marked (a Hello), returning its address as text.
bool NetSocketLearnsPeer();
void NetSocketKeepSender();
const char *NetSocketAnswerHello();

// The learnt peer's address as text ("" before one is learnt).
const char *NetSocketPeerText();

// Sends a datagram to the peer. With DC_NETPLAY_LAG_MS, DC_NETPLAY_JITTER_MS or DC_NETPLAY_LOSS_PCT
// set (tests), each datagram is held that long (plus up to the jitter, so they can come out of order)
// or dropped at that rate first, from a generator of its own seeded by DC_NETPLAY_NET_SEED. coop-steamrate: false only
// when Steam refused it for a full send buffer (not sent: send it again later); a datagram the impairment holds or drops
// counts as sent.
bool NetSocketSend(const void *data, std::size_t size);

// coop-apartsync (a test's, DC_NETPLAY_TEST_BLACKOUT): nothing this copy sends leaves it for ms, as on a network that cut out.
void NetSocketBlackout(int ms);

// Datagrams sent and dropped by the injected loss so far.
void NetSocketCounts(long long &sent, long long &dropped);

// coop-steamjoin: over Steam, the bytes Steam holds unsent to the peer and the rate it sends them at (bytes a second);
// false over UDP, which keeps no queue of its own (NetSteamQueue).
bool NetSocketQueue(int &pending_bytes, int &rate);

// coop-steamceiling: over Steam, Steam's view of the link as one log line (NetSteamLine); false over UDP.
bool NetSocketSteamLine(char *line, std::size_t size);

// Bytes of the next datagram waiting, copied into data; 0 when none is.
int NetSocketReceive(void *data, std::size_t size);

// Whether text is an IPv4 address (four numbers 0-255 with dots, not 0.0.0.0).
bool NetSocketValidAddress(const char *text);

// This PC's address on the network its outgoing route leaves by (a LAN address on a home network),
// found once without sending anything; "" when there is none.
const char *NetSocketLocalAddress();

// The host's internet (WAN) address by STUN from the link's own socket (server "host:port", or "off"): asked once,
// in the background; NetSocketWan is "" until the answer comes (port: the router's mapped port). Likely CGNAT: a
// 100.64.0.0/10 answer, or a mapped port unlike this PC's (joining by address will not work; Steam will).
void        NetSocketStunStart(const char *server, int local_port);
// coop-cand58: the STUN asker's thread stopped and joined (NetplayStopThreads); true when there was one.
bool        NetSocketStunStop();
const char *NetSocketWan(int &port);
bool        NetSocketWanLikelyCgnat();

// A NAT punch: 4 bytes from the link's socket to address:port, so this side's router lets that peer's datagrams in
// (a NAT code, netplay.cpp). The other side's NetSocketReceive drops it. NetSocketOpen on the port already bound keeps
// the socket (the mapping stays); a peer "a.b.c.d:port" is aimed at that port.
void NetSocketPunch(const char *address, int port);
// coop-punchlog: UDP datagrams sent (punches counted) and heard (all but STUN's answers, punches counted) since the
// game started, and the address:port the last heard came from, for a NAT join's log.
void NetSocketUdpCounts(long long &sent, long long &heard, const char *&last_from);
// coop-pt1108s: the datagrams heard from an address this socket had not sent to (they prove a forwarded port), and the last one's sender.
void NetSocketUdpFresh(long long &fresh, const char *&from);
// coop-natonly test hook: from now on every datagram but STUN's is dropped as it comes (DC_NETPLAY_TEST_NAT_BLOCK's
// drop, from a moment in play on: a direct link cut for good).
void NetSocketTestCut();
// coop-natonly: Steam carries a NAT > Steam host's codes only ("DCNAT"); anything else that comes over it is dropped (a
// guest that came over Steam is never answered).
void NetSocketSteamCodesOnly(bool on);
// The last "DCNAT <code>" a Steam guest sent this host (Host > NAT > Steam), "" before one: a code to punch towards.
const char *NetSocketNatHeard();
// Joining by a NAT code whose internet address is this PC's own (both on one network, where a router that does not
// loop its own address back never passes the punch): NetSocketLanBroadcast sends to 255.255.255.255:port too, and
// NetSocketTakeLanSender, once a Hello has come, aims at its sender from then on ("a.b.c.d:port", nullptr when it was
// the code's address anyway or not looking) and stops looking. Port 0 stops looking.
void        NetSocketLanSearch(int port);
void        NetSocketLanBroadcast(const void *data, std::size_t size);
const char *NetSocketTakeLanSender();
