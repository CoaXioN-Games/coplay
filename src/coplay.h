/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

//================================================
// CoaXioN Source SDK p2p networking: "CoaXioN Coplay"
// Author : Tholp / Jackson S
//================================================

//Shared defines

// Ok, here the rundown on the concept behind this
// After socket opening and all the handshake stuff:
// The client has a UDP listener that relays all packets -
// through the Steam datagram, the server machine recieves them and -
// sends the UDP packets locally to the game server which has a similar mechanism to send back to the client.
// This allows us to make use of the P2P features steam offers within the Source SDK without any networking code changes
#pragma once
#ifndef COPLAY_H
#define COPLAY_H

#define COPLAY_MSG_COLOR Color(170, 255, 0, 255)
#define COPLAY_DEBUG_MSG_COLOR Color(255, 170, 0, 255)

#define COPLAY_MAX_PACKETS 16

//YYYY-MM-DD-(a-z) if theres multiple in a day
#define COPLAY_VERSION "2024-07-06-a"

//For vpcless quick testing, leave this commented when commiting
//#define COPLAY_USE_LOBBIES
//#undef COPLAY_USE_LOBBIES

#include "steam/isteamnetworkingsockets.h"
#include "steam/isteamnetworkingutils.h"
#include "steam/isteamfriends.h"
#include "steam/steam_api.h"
#ifdef COPLAY_USE_LOBBIES
#include "steam/isteammatchmaking.h"
#endif

#include "tier0/valve_minmax_off.h"	// GCC 4.2.2 headers screw up our min/max defs.
#include <string>
#include "tier0/valve_minmax_on.h"

enum JoinFilter
{
    eP2PFilter_OFF = -1,
    eP2PFilter_CONTROLLED = 0,// requires a password appended to coplay_connect to make a connection
                              // given by the host running coplay_getconnectcommand
                              // passwords are not user settable and are randomized every socket open or
                              // when running coplay_rerandomize_password
    eP2PFilter_FRIENDS = 1,
    eP2PFilter_EVERYONE = 2,
};

#define COPLAY_NETMSG_NEEDPASS "NeedPass"
#define COPLAY_NETMSG_OK "OK"

enum ConnectionRole
{
    eConnectionRole_UNAVAILABLE = -1,// Waiting on Steam
    eConnectionRole_NOT_CONNECTED = 0,
    eConnectionRole_HOST,
    eConnectionRole_CLIENT
};

enum ConnectionEndReason // see the enum ESteamNetConnectionEnd in steamnetworkingtypes.h
{
    k_ESteamNetConnectionEnd_App_NotOpen = 1001,
    k_ESteamNetConnectionEnd_App_ServerFull,
    k_ESteamNetConnectionEnd_App_RemoteIssue,//couldn't open a socket
    k_ESteamNetConnectionEnd_App_ClosedByPeer,

    // incoming connection rejected
    k_ESteamNetConnectionEnd_App_NotFriend,
    k_ESteamNetConnectionEnd_App_BadPassword,
};

struct PendingConnection// for when we make a steam connection to ask for a password but
                        // not letting it send packets to the game server yet
{
    HSteamNetConnection SteamConnection = 0;
    float               TimeCreated = 0;
};
#endif
