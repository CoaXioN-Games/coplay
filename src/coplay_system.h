/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

//================================================
// CoaXioN Implementation of Steam P2P networking on Source SDK: "CoaXioN Coplay"
// Author : Tholp / Jackson S
//================================================

#ifndef COPLAY_SYSTEM_H
#define COPLAY_SYSTEM_H
#pragma once

#include "coplay.h"
#include "igamesystem.h"
#include "steam/isteamnetworkingsockets.h"
#include "steam/isteamnetworkingutils.h"
#include "tier1/utlvector.h"
#include "coplay_connection.h"
#include "coplay_client.h"
#include "coplay_host.h"

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

    ConnectionRole GetRole() { return m_role;  }
    CCoplayClient* GetClient() {return &m_client; }
    CCoplayHost*   GetHost() { return &m_host; }

    CON_COMMAND_MEMBER_F(CCoplaySystem, "coplay_connect", CoplayConnect, "Connect to a Coplay game", FCVAR_NONE);

    std::string GetConnectCommand();
private:
	void SetRole(ConnectionRole role);
	void ConnectToHost(CSteamID host, std::string passcode = "");
	void OnListLobbiesCmd(LobbyMatchList_t *pLobbyMatchList, bool IOFailure);


private:
    // Callbacks
    STEAM_CALLBACK(CCoplaySystem, ConnectionStatusUpdated, SteamNetConnectionStatusChangedCallback_t);
    STEAM_CALLBACK(CCoplaySystem, JoinGame,                GameRichPresenceJoinRequested_t);
#ifdef COPLAY_USE_LOBBIES
    STEAM_CALLBACK(CCoplaySystem, LobbyJoined,             LobbyEnter_t);
    STEAM_CALLBACK(CCoplaySystem, LobbyJoinRequested,      GameLobbyJoinRequested_t);
#else
    // So you can still call into these if you want to for some reason
    void LobbyJoined(LobbyEnter_t* pParam);
    void LobbyJoinRequested(GameLobbyJoinRequested_t *pParam);
#endif
    CCallResult< CCoplaySystem, LobbyMatchList_t> m_lobbyListResult;

private:
    // Commands
	CON_COMMAND_MEMBER_F(CCoplaySystem, "coplay_opensocket", OpenSocket, "Manually (re)open your game to P2P connections", FCVAR_NONE);
    CON_COMMAND_MEMBER_F(CCoplaySystem, "coplay_closesocket", CloseSocket, "Manually close your game to P2P connections", FCVAR_NONE);
	CON_COMMAND_MEMBER_F(CCoplaySystem, "coplay_about", PrintAbout, "", FCVAR_NONE);
	CON_COMMAND_MEMBER_F(CCoplaySystem, "coplay_invite", InvitePlayer, "Prints a command for other people to join you", FCVAR_NONE);
	CON_COMMAND_MEMBER_F(CCoplaySystem, "coplay_rerandomize_password", ReRandomizePassword, "Randomizes the password given by coplay_getconnectcommand", FCVAR_NONE);
	CON_COMMAND_MEMBER_F(CCoplaySystem, "coplay_status", PrintStatus, "", FCVAR_NONE);

#ifdef COPLAY_USE_LOBBIES
    CON_COMMAND_MEMBER_F(CCoplaySystem, "coplay_listlobbies", ListLobbies, "List all joinable lobbies", FCVAR_NONE);
    CON_COMMAND_MEMBER_F(CCoplaySystem, "connect_lobby", ConnectToLobby, "", FCVAR_HIDDEN);
#endif

private:
	static CCoplaySystem* s_instance;
    FnCommandCallback_t m_oldConnectCallback;

    ConnectionRole m_role;

	CCoplayClient	   m_client;
	CCoplayHost		   m_host;
};
#endif
