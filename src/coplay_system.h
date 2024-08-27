/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef COPLAY_SYSTEM_H
#define COPLAY_SYSTEM_H
#pragma once

#include "coplay.h"
#include "igamesystem.h"
#include "steam/isteamnetworkingsockets.h"
#include "steam/isteamnetworkingutils.h"
#include "tier1/utlvector.h"
#include "coplay_connection.h"

struct PendingConnection// for when we make a steam connection to ask for a password but
    // not letting it send packets to the game server yet
{
    HSteamNetConnection hSteamConnection = 0;
    float               timeCreated = 0;
};

//Handles all the Steam callbacks and connection management
class CCoplaySystem : public CAutoGameSystemPerFrame
{
public:
    virtual bool Init();
    virtual void Update(float frametime);
    virtual void Shutdown();
    virtual void PostInit();

    virtual void LevelInitPostEntity();
    virtual void LevelShutdownPreEntity();

    void        OpenP2PSocket();
    void        CloseP2PSocket();
    void        CloseAllConnections(bool waitforjoin = false);
    bool        CreateSteamConnectionTuple(HSteamNetConnection hConn);

    int         GetConnectCommand(std::string &out);// 0: OK, 1: Not Hosting, 2: Use Coplay_invite instead

    ConnectionRole GetRole(){return m_role;}
    void           SetRole(ConnectionRole newrole);
#ifdef COPLAY_USE_LOBBIES
    CSteamID    GetLobby(){return m_lobby;}
#else
    std::string         GetPassword(){return m_password;}
    void                RechoosePassword();
#endif

    uint32         m_msSleepTime = 3;

private:
    ConnectionRole      m_role = eConnectionRole_UNAVAILABLE;
    HSteamListenSocket  m_hP2PSocket;
#ifdef COPLAY_USE_LOBBIES
    CSteamID            m_lobby;
#else
public:
    std::string                m_password;// we use this same variable for a password we need to send if we're the client, or the one we need to check agaisnt if we're the server
    CUtlVector<PendingConnection> m_pendingConnections; // cant connect to the server but has a steam connection to send a password

#endif
public:
    CUtlVector<CCoplayConnection*> m_connections;

    CCallResult<CCoplaySystem, LobbyMatchList_t> m_lobbyListResult;
    void OnLobbyListcmd( LobbyMatchList_t *pLobbyMatchList, bool bIOFailure);

private:
    STEAM_CALLBACK(CCoplaySystem, ConnectionStatusUpdated, SteamNetConnectionStatusChangedCallback_t);
    STEAM_CALLBACK(CCoplaySystem, JoinGame,                GameRichPresenceJoinRequested_t);
#ifdef COPLAY_USE_LOBBIES
    STEAM_CALLBACK(CCoplaySystem, LobbyCreated,            LobbyCreated_t);
    STEAM_CALLBACK(CCoplaySystem, LobbyJoined,             LobbyEnter_t);
    STEAM_CALLBACK(CCoplaySystem, LobbyJoinRequested,      GameLobbyJoinRequested_t);
#endif
};

extern CCoplaySystem* g_pCoplayConnectionHandler;
#endif
