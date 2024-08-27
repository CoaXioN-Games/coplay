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
    CCoplaySystem(const CCoplaySystem& other) = delete;
    void operator=(const CCoplaySystem&) = delete;
public:
    CCoplaySystem();

    static CCoplaySystem* GetInstance();

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
    CSteamID    GetLobby(){return m_lobby;}
    std::string         GetPassword(){return m_password;}
    void                RechoosePassword();

    void OnLobbyListcmd( LobbyMatchList_t *pLobbyMatchList, bool bIOFailure);

    // Commands
    CON_COMMAND_MEMBER_F(CCoplaySystem, "coplay_connect", CoplayConnect, "Connect to a Coplay game", FCVAR_NONE);
	CON_COMMAND_MEMBER_F(CCoplaySystem, "coplay_opensocket", OpenSocket, "Manually (re)open your game to P2P connections", FCVAR_NONE);
    CON_COMMAND_MEMBER_F(CCoplaySystem, "coplay_closesocket", CloseSocket, "Manually close your game to P2P connections", FCVAR_NONE);
    CON_COMMAND_MEMBER_F(CCoplaySystem, "coplay_listlobbies", ListLobbies, "List all joinable lobbies", FCVAR_NONE);

	CON_COMMAND_MEMBER_F(CCoplaySystem, "coplay_about", PrintAbout, "", FCVAR_NONE);
	CON_COMMAND_MEMBER_F(CCoplaySystem, "coplay_getconnectcommand", GetConnectCommand, "Prints a command for other people to join you", FCVAR_NONE);
	CON_COMMAND_MEMBER_F(CCoplaySystem, "coplay_rerandomize_password", ReRandomizePassword, "Randomizes the password given by coplay_getconnectcommand", FCVAR_NONE);
    CON_COMMAND_MEMBER_F(CCoplaySystem, "connect_lobby", ConnectToLobby, "", FCVAR_HIDDEN);
    CON_COMMAND_MEMBER_F(CCoplaySystem, "coplay_invite", InviteToLobby, "", FCVAR_NONE);

	// Debug commands
	CON_COMMAND_MEMBER_F(CCoplaySystem, "coplay_debug_printstate", DebugPrintState, "", FCVAR_NONE);
	CON_COMMAND_MEMBER_F(CCoplaySystem, "coplay_debug_createdummyconnection", DebugCreateDummyConnection, "Create a empty connection", FCVAR_DEVELOPMENTONLY);
	CON_COMMAND_MEMBER_F(CCoplaySystem, "coplay_debug_senddummy_steam", DebugSendDummySteam, "", FCVAR_CLIENTDLL);
	CON_COMMAND_MEMBER_F(CCoplaySystem, "coplay_listinterfaces", ListInterfaces, "", FCVAR_CLIENTDLL);

private:
    STEAM_CALLBACK(CCoplaySystem, ConnectionStatusUpdated, SteamNetConnectionStatusChangedCallback_t);
    STEAM_CALLBACK(CCoplaySystem, JoinGame,                GameRichPresenceJoinRequested_t);
    STEAM_CALLBACK(CCoplaySystem, LobbyCreated,            LobbyCreated_t);
    STEAM_CALLBACK(CCoplaySystem, LobbyJoined,             LobbyEnter_t);
    STEAM_CALLBACK(CCoplaySystem, LobbyJoinRequested,      GameLobbyJoinRequested_t);

public:
    std::string                m_password;// we use this same variable for a password we need to send if we're the client, or the one we need to check agaisnt if we're the server
    CUtlVector<PendingConnection> m_pendingConnections; // cant connect to the server but has a steam connection to send a password
    CUtlVector<CCoplayConnection*> m_connections;
    CCallResult<CCoplaySystem, LobbyMatchList_t> m_lobbyListResult;

private:
	static CCoplaySystem* s_instance;

    ConnectionRole      m_role = eConnectionRole_UNAVAILABLE;
    HSteamListenSocket  m_hP2PSocket;
    CSteamID            m_lobby;
    FnCommandCallback_t m_oldConnectCallback;
};
#endif
