#pragma once

#include "coop/game_ns.hpp"

// coop-upnp: a host asks its router to forward the link's UDP port to this PC by UPnP (an Internet Gateway Device's
// WANIPConnection or WANPPPConnection service), so a friend outside the network can join by address or NAT code with
// no manual port forward. Multi-player > Host > IP (and NAT > Code) does it unless game.netplay_upnp is false;
// DC_NETPLAY_UPNP=1 or 0 overrides (every test copy runs with 0: a test never asks the real router). A port already
// forwarded to this PC by hand (the router's table says so) is left as it is. Standard SSDP (a multicast
// M-SEARCH to 239.255.255.250:1900) finds the router; SOAP over HTTP asks it. Everything runs on a thread of its own and
// logs "netplay: upnp: ..." lines; nothing waits on it but the removal at exit (about a second at most).
//
// The mapping is leased (an hour, renewed at half of that) so a crashed copy's mapping lapses by itself; a router that
// takes only permanent leases (error 725) gets one, re-added every half hour, and the copy removes it when it quits.
//
// Tests: DC_NETPLAY_UPNP_SSDP=address:port sends the M-SEARCH there (qa\relay\upnp_mock.py) instead of the multicast
// group; DC_NETPLAY_UPNP_LEASE_S sets the lease. Twins: DC_NETPLAY_MUTATE=upnp_no_renew (never renewed),
// upnp_no_remove (left on the router at exit), upnp_ignore_forward (a port forwarded by hand not read).

// Starts the mapping of UDP port (this PC's) once, in the background; a second call does nothing.
void NetUpnpStart(int port);

// Once mapped, or found already forwarded: the router's outside address and the outside port ("" before, or when it
// failed).
const char *NetUpnpOutside(int &port);

// Where it is: not asked, asking, mapped (ours, renewed, removed at exit), already forwarded to this PC (left alone),
// failed (NetUpnpWhy: a few words for the waiting line, "no UPnP router" ...).
enum { kUpnpIdle, kUpnpTrying, kUpnpMapped, kUpnpForwarded, kUpnpFailed };
int         NetUpnpState();
const char *NetUpnpWhy();

// Whether the mapping's outside address is a public one: not 10/8, 172.16/12, 192.168/16, 100.64/10 or 127/8 (a router
// behind another NAT or a carrier's, where the mapping does not reach the internet).
bool NetUpnpPublic();

// Removes the mapping now (the exit's handler calls it too).
void NetUpnpStop();
