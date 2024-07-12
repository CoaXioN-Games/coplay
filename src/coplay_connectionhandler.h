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

    ConnectionRole GetRole() { return Role; }
    void           SetRole(ConnectionRole newrole);

    void OnLobbyListcmd(LobbyMatchList_t* pLobbyMatchList, bool bIOFailure);

    STEAM_CALLBACK(CCoplayConnectionHandler, ConnectionStatusUpdated, SteamNetConnectionStatusChangedCallback_t);
    STEAM_CALLBACK(CCoplayConnectionHandler, JoinGame, GameRichPresenceJoinRequested_t);

#ifdef COPLAY_USE_LOBBIES
    CSteamID    GetLobby() { return Lobby; }
    bool IsUserInLobby(CSteamID LobbyID, CSteamID UserID);
    STEAM_CALLBACK(CCoplayConnectionHandler, LobbyCreated, LobbyCreated_t);
    STEAM_CALLBACK(CCoplayConnectionHandler, LobbyJoined, LobbyEnter_t);
    STEAM_CALLBACK(CCoplayConnectionHandler, LobbyJoinRequested, GameLobbyJoinRequested_t);
#else
    std::string         GetPassword() { return Password; }
    void                RechoosePassword();
#endif

private:
    uint32         msSleepTime = 3;
    ConnectionRole      Role = eConnectionRole_UNAVAILABLE;
    HSteamListenSocket  HP2PSocket;
#ifdef COPLAY_USE_LOBBIES
    CSteamID            Lobby;
#else
    std::string                Password;// we use this same variable for a password we need to send if we're the client, or the one we need to check agaisnt if we're the server
    CUtlVector<PendingConnection> PendingConnections; // cant connect to the server but has a steam connection to send a password
#endif
    CUtlVector<CCoplayConnection*> Connections;
    CCallResult<CCoplayConnectionHandler, LobbyMatchList_t> LobbyListResult;
};
#endif