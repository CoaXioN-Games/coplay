#ifndef COPLAY_CONNECTIONHANDLER_H
#define COPLAY_CONNECTIONHANDLER_H
#pragma once

#include "igamesystem.h"
#include <coplay.h>
#include "coplay_connection.h"

//Handles all the Steam callbacks and connection management
class CCoplayConnectionHandler : public CAutoGameSystemPerFrame
{
public:
    CCoplayConnectionHandler();

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

    int         GetConnectCommand(std::string& out);// 0: OK, 1: Not Hosting, 2: Use Coplay_invite instead

    ConnectionRole GetRole() { return m_role; }
    void           SetRole(ConnectionRole newrole);

    void OnLobbyListcmd(LobbyMatchList_t* pLobbyMatchList, bool bIOFailure);

    static void UpdateSleepTime(IConVar* var, const char* pOldValue, float flOldValue);

    STEAM_CALLBACK(CCoplayConnectionHandler, ConnectionStatusUpdated, SteamNetConnectionStatusChangedCallback_t);
    STEAM_CALLBACK(CCoplayConnectionHandler, JoinGame, GameRichPresenceJoinRequested_t);

#ifdef COPLAY_USE_LOBBIES
    CSteamID    GetLobby() { return m_lobbyID; }
    bool IsUserInLobby(CSteamID LobbyID, CSteamID UserID);
    STEAM_CALLBACK(CCoplayConnectionHandler, LobbyCreated, LobbyCreated_t);
    STEAM_CALLBACK(CCoplayConnectionHandler, LobbyJoined, LobbyEnter_t);
    STEAM_CALLBACK(CCoplayConnectionHandler, LobbyJoinRequested, GameLobbyJoinRequested_t);
#else
    std::string         GetPassword() { return Password; }
    void                RechoosePassword();
#endif

    // Commands
	CON_COMMAND_MEMBER_F(CCoplayConnectionHandler, "coplay_connect", CoplayConnect, "Connect to a Coplay game", FCVAR_NONE);
    //CON_COMMAND_MEMBER_F(CCoplayConnectionHandler, "coplay_opensocket", OpenSocket, "Disconnect from a Coplay game", FCVAR_NONE);


#ifdef DEBUG
#endif


private:
    uint32         m_sleepTimeMilli;
    ConnectionRole      m_role;
    HSteamListenSocket  m_HP2PSocket;
#ifdef COPLAY_USE_LOBBIES
    CSteamID            m_lobbyID;
#else
    std::string                m_password;// we use this same variable for a password we need to send if we're the client, or the one we need to check agaisnt if we're the server
    CUtlVector<PendingConnection> m_pendingConnections; // cant connect to the server but has a steam connection to send a password
#endif
    CUtlVector<CCoplayConnection*> m_connections;
    CCallResult<CCoplayConnectionHandler, LobbyMatchList_t> m_lobbyListResult;

	FnCommandCallback_t m_oldConnectCallback;
};
#endif