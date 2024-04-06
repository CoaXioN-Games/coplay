/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

//================================================
// CoaXioN Source SDK p2p networking: "CoaXioN Coplay"
// Author : Tholp / Jackson S
// File Last Modified : Apr 5 2024
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

#include <cbase.h>
#include "SDL2/SDL_net.h"
#include "steam/isteamnetworkingsockets.h"
#include "steam/isteamnetworkingutils.h"
#include "steam/isteamfriends.h"
#include "steam/steam_api.h"

enum JoinFilter
{
    eP2PFilter_NONE = -1,
    eP2PFilter_EVERYONE = 0,
    eP2PFilter_FRIENDS,
    eP2PFilter_INVITEONLY,
};

enum ConnectionRole
{
    eConnectionRole_UNAVAILABLE = -1,
    eConnectionRole_NOT_CONNECTED = 0,
    eConnectionRole_HOST,
    eConnectionRole_CLIENT
};

enum ConnectionEndReason //see the enum ESteamNetConnectionEnd in steamnetworkingtypes.h
{
    k_ESteamNetConnectionEnd_App_NotOpen = 1001,
    k_ESteamNetConnectionEnd_App_ServerFull,
    k_ESteamNetConnectionEnd_App_PortsFilled, //every port we tried is already bound, somehow
    k_ESteamNetConnectionEnd_App_ClosedByPeer,
};

extern ConVar coplay_joinfilter;
extern ConVar coplay_timeoutduration;
extern ConVar coplay_connectionthread_hz;

extern ConVar coplay_debuglog_socketcreation;
extern ConVar coplay_debuglog_socketspam;
extern ConVar coplay_debuglog_steamconnstatus;

static uint32 SwapEndian32(uint32 num)
{
    byte newnum[4];
    newnum[0] = ((byte*)&num)[3];
    newnum[1] = ((byte*)&num)[2];
    newnum[2] = ((byte*)&num)[1];
    newnum[3] = ((byte*)&num)[0];
    return *((uint32*)newnum);
}

static uint16 SwapEndian16(uint16 num)
{
    byte newnum[2];
    newnum[0] = ((byte*)&num)[1];
    newnum[1] = ((byte*)&num)[0];
    return *((uint16*)newnum);
}

class CCoplayConnection : public CThread
{
    int Run();
public:
    CCoplayConnection(HSteamNetConnection hConn);

    UDPsocket LocalSocket = NULL;
    uint16    Port = 0;
    IPaddress SendbackAddress;
    HSteamNetConnection    SteamConnection = 0;

    void QueueForDeletion(){DeletionQueued = true;}
    virtual void OnExit();

private:
    bool DeletionQueued = false;

    float LastPacketTime = 0;//This is for when the steam connection is still being kept alive but there is no actual activity
};

class CCoplayConnectionHandler : public CAutoGameSystemPerFrame
{
public:
    CCoplayConnectionHandler();

    virtual void Update(float frametime);

    virtual void Shutdown()
    {
        CloseAllConnections(true);
    }

    void        OpenP2PSocket();
    void        CloseP2PSocket();
    void        CloseAllConnections(bool waitforjoin = false);
    bool        CreateSteamConnectionTuple(HSteamNetConnection hConn);

    ConnectionRole GetRole(){return Role;}
    void           SetRole(ConnectionRole newrole);

    uint32         msSleepTime;

private:
    ConnectionRole Role = eConnectionRole_UNAVAILABLE;
    HSteamListenSocket  HP2PSocket;
public:
    CUtlVector<CCoplayConnection*> Connections;

private:
    STEAM_CALLBACK(CCoplayConnectionHandler, ConnectionStatusUpdated, SteamNetConnectionStatusChangedCallback_t);
#ifdef CLIENT_DLL
    STEAM_CALLBACK(CCoplayConnectionHandler, JoinGame, GameRichPresenceJoinRequested_t);
#endif
};
extern CCoplayConnectionHandler *g_pCoplayConnectionHandler;

#endif
