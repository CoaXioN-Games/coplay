/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

//================================================
// CoaXioN Source SDK p2p networking: "CoaXioN Coplay"
// Author : Tholp / Jackson S
// File Last Modified : Apr 16 2024
//================================================

// Make, delete and keep track of steam connections & lobbies

#include "cbase.h"
#include <coplay.h>
#include <inetchannel.h>
#include <inetchannelinfo.h>
#include <steam/isteamgameserver.h>

static char *queuedcommand = NULL;

static void UpdateSleepTime()
{
    if (g_pCoplayConnectionHandler)
    {
        if (coplay_connectionthread_hz.GetFloat() > 0)
            g_pCoplayConnectionHandler->msSleepTime = 1000/coplay_connectionthread_hz.GetFloat();
        else
            g_pCoplayConnectionHandler->msSleepTime = 0;
    }
}

static void ChangeLobbyType()
{
    if (g_pCoplayConnectionHandler && g_pCoplayConnectionHandler->GetLobby().ConvertToUint64())
        SteamMatchmaking()->SetLobbyType(g_pCoplayConnectionHandler->GetLobby(), (ELobbyType)coplay_lobbytype.GetInt());
}

ConVar coplay_lobbytype("coplay_lobbytype", "1", FCVAR_ARCHIVE, "Whos allowed to connect to our Server?\n"
                         "0  : Invite Only\n"
                         "1  : Steam Friends  or Invite\n"
                         "2  : Public, will be advertised\n",
                        true, 0, true, 2, (FnChangeCallback_t)ChangeLobbyType);// See the enum ELobbyType in isteammatchmaking.h
ConVar coplay_connectionthread_hz("coplay_connectionthread_hz", "300", FCVAR_ARCHIVE, "Number of times to run a connection per second.\n", (FnChangeCallback_t)UpdateSleepTime);

ConVar coplay_debuglog_socketcreation("coplay_debuglog_socketcreation", "0", 0, "Prints more information when a socket is opened or closed.\n");
ConVar coplay_debuglog_steamconnstatus("coplay_debuglog_steamconnstatus", "0", 0, "Prints more detailed steam connection statuses.\n");


CCoplayConnectionHandler *g_pCoplayConnectionHandler;

class CCoplaySteamBootstrap : public CAutoGameSystem
// we need to insure the steam api is loaded before we make any objects that need callbacks ( CCoplayConnectionHandler )
{
public:
    virtual bool Init()
    {
    #ifdef GAME_DLL //may support dedicated servers at some point
        if (!engine->IsDedicatedServer())
        {
            Remove(this);
            return true;
        }

    #endif
        if (SDL_Init(0))
        {
            Error("SDL Failed to Initialize: \"%s\"", SDL_GetError());
            return false;
        }
        if (SDLNet_Init())
        {
            Error("SDLNet Failed to Initialize: \"%s\"", SDLNet_GetError());
            return false;
        }
        if (!SteamAPI_Init())
        {
            Error("Steam API Failed to Init! (%s::%i)\n", __FILE__, __LINE__);
        }
        SteamNetworkingUtils()->InitRelayNetworkAccess();

        ConColorMsg(COPLAY_MSG_COLOR, "[Coplay] Initialization started...\n");
        g_pCoplayConnectionHandler = new CCoplayConnectionHandler;
        return true;
    }
}coplaybootstrap;

CCoplayConnectionHandler::CCoplayConnectionHandler()
{
    UpdateSleepTime();
}

void CCoplayConnectionHandler::Update(float frametime)
{
    static bool checkavail = true;
    static float lastSteamRPCUpdate = 0.0f;
    SteamAPI_RunCallbacks();
    if (checkavail) //The callback specifically to check this is registered by the engine already
    {
        SteamRelayNetworkStatus_t deets;
        if ( SteamNetworkingUtils()->GetRelayNetworkStatus(&deets) == k_ESteamNetworkingAvailability_Current)
        {
            checkavail = false;
            SetRole(eConnectionRole_NOT_CONNECTED);
            ConColorMsg(COPLAY_MSG_COLOR, "[Coplay] Steam Relay Connection successful!\n");
            ConColorMsg(COPLAY_MSG_COLOR, "[Coplay] Intialization Complete!\n");

            if (queuedcommand != NULL)
            {
                ConColorMsg(COPLAY_MSG_COLOR, "%s\n", queuedcommand);
                engine->ClientCmd(queuedcommand);
                free(queuedcommand);
            }
        }
    }

    if (gpGlobals->realtime > lastSteamRPCUpdate + 1.0f)
    {
        // if (engine->IsConnected())
        // {
        //     INetChannel *pChannel = reinterpret_cast< INetChannel * >(engine->GetNetChannelInfo());

        //     char szIP[32];
        //     if( pChannel->IsLoopback() && HP2PSocket)
        //     {
        //         //SteamNetworkingIdentity netID;
        //         if (Lobby.ConvertToUint64())//SteamNetworkingSockets()->GetIdentity(&netID)
        //         {
        //             V_snprintf(szIP, sizeof(szIP), "%llu", Lobby.ConvertToUint64());
        //         }
        //         else
        //         {
        //             lastSteamRPCUpdate = gpGlobals->realtime;
        //             return;
        //         }
        //     }
        //     else if( pChannel )
        //     {
        //         V_snprintf(szIP, sizeof(szIP), "%s", pChannel->GetAddress());
        //     }
        //     else
        //     {
        //         SteamFriends()->SetRichPresence("connect", "");
        //         lastSteamRPCUpdate = gpGlobals->realtime;
        //         return;
        //     }
        //     char szConnectCMD[64] = "+coplay_connect ";
        //     V_strcat(szConnectCMD, szIP, sizeof(szConnectCMD));
        //     SteamFriends()->SetRichPresence("connect", szConnectCMD);

        // } //Lobbies do connection rpc for us now
        if (Role == eConnectionRole_HOST && Lobby.ConvertToUint64())
        {
            ConVarRef hostname("hostname");
            char map[256];
            V_StrRight(engine->GetLevelName(), -5, map, sizeof(map));// remove "maps/"
            V_StrLeft(map, -4, map, sizeof(map));// remove ".bsp"
            SteamMatchmaking()->SetLobbyData(Lobby, "map", map);
            SteamMatchmaking()->SetLobbyData(Lobby, "hostname", hostname.GetString());
        }
        lastSteamRPCUpdate = gpGlobals->realtime;
    }
}

void CCoplayConnectionHandler::OpenP2PSocket()
{
    CloseP2PSocket();
    SetRole(eConnectionRole_HOST);
    HP2PSocket =  SteamNetworkingSockets()->CreateListenSocketP2P(0, 0, NULL);
    ELobbyType lobbytype = k_ELobbyTypePublic;//coplay_lobbymode.GetInt();
    SteamMatchmaking()->CreateLobby(lobbytype, 33);// Let maxplayers do this
}

void CCoplayConnectionHandler::CloseP2PSocket()
{
    CloseAllConnections();
    SteamNetworkingSockets()->CloseListenSocket(HP2PSocket);
    HP2PSocket = 0;
    SteamMatchmaking()->LeaveLobby(Lobby);
    Lobby.SetFromUint64( 0 );
    SetRole(eConnectionRole_CLIENT);
}

void CCoplayConnectionHandler::CloseAllConnections(bool waitforjoin)
{
    for (int i = 0; i< Connections.Count(); i++)
        Connections[i]->QueueForDeletion();
    if (waitforjoin)
        for (int i = 0; i< Connections.Count(); i++)
            Connections[i]->Join();
}

CON_COMMAND_F(coplay_opensocket, "Open p2p listener", FCVAR_CLIENTDLL)
{
    g_pCoplayConnectionHandler->OpenP2PSocket();
}

bool CCoplayConnectionHandler::CreateSteamConnectionTuple(HSteamNetConnection hConn)
{
    SteamNetConnectionInfo_t newinfo;
    if (!SteamNetworkingSockets()->GetConnectionInfo(hConn, &newinfo))
        return false;

    bool alreadyconnected = false;
    for (int i = 0; i< Connections.Count(); i++)
    {
        SteamNetConnectionInfo_t info;
        if (SteamNetworkingSockets()->GetConnectionInfo(Connections[i]->SteamConnection, &info))
        {
            if (info.m_identityRemote.GetSteamID64() == newinfo.m_identityRemote.GetSteamID64())
            {
                Connections[i]->SteamConnection = hConn;
                alreadyconnected = true;
                break;
            }
        }
    }

    if (!alreadyconnected)
    {
        CCoplayConnection *connection = new CCoplayConnection(hConn);

        connection->Start();
        Connections.AddToTail(connection);
    }


    if (Role == eConnectionRole_CLIENT)
    {
        char cmd[64];
        uint32 ipnum = Connections.Tail()->SendbackAddress.host;
        V_snprintf(cmd, sizeof(cmd), "connect %i.%i.%i.%i:%i", ((uint8*)&ipnum)[0], ((uint8*)&ipnum)[1], ((uint8*)&ipnum)[2], ((uint8*)&ipnum)[3], Connections.Tail()->Port);
        engine->ClientCmd(cmd);
    }

    return true;
}

void CCoplayConnectionHandler::SetRole(ConnectionRole newrole)
{
    ConVarRef sv_lan("sv_lan");
    ConVarRef engine_no_focus_sleep("engine_no_focus_sleep");
    //ConVarRef net_usesocketsforloopback("net_usesocketsforloopback");
    switch (newrole)
    {
    case eConnectionRole_HOST:
        sv_lan.SetValue("1");//sv_lan off will heartbeat the server and allow clients to see our ip
        engine_no_focus_sleep.SetValue("0"); // without this, if the host tabs out everyone lags
        //net_usesocketsforloopback.SetValue("1");
        break;
    case eConnectionRole_CLIENT:
        engine_no_focus_sleep.SetValue(engine_no_focus_sleep.GetDefault());
        //net_usesocketsforloopback.SetValue("1");
        break;
    case eConnectionRole_NOT_CONNECTED:
        //net_usesocketsforloopback.SetValue("0");
        CloseAllConnections();
    }


    Role = newrole;
}

CON_COMMAND_F(coplay_debug_createdummyconnection, "Create a empty connection", 0)
{
    //g_pCoplayConnectionHandler->CreateSteamConnectionTuple(NULL);
    CCoplayConnection *connection = new CCoplayConnection(NULL);

    connection->Start();
    g_pCoplayConnectionHandler->Connections.AddToTail(connection);
}

void CCoplayConnectionHandler::ConnectionStatusUpdated(SteamNetConnectionStatusChangedCallback_t* pParam)
{
    SteamNetworkingIdentity ID = pParam->m_info.m_identityRemote;
    if (coplay_debuglog_steamconnstatus.GetBool())
        ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "[Coplay Debug] Steam Connection status updated: Coplay role: %i, Peer SteamID64: %llu,\n Param: %i\n Old Param: %i\n",
                    GetRole(), pParam->m_info.m_identityRemote.GetSteamID64(), pParam->m_info.m_eState, pParam->m_eOldState);

    switch (pParam->m_info.m_eState)
    {
    case k_ESteamNetworkingConnectionState_Connecting:
        if (GetRole() == eConnectionRole_HOST)
        {
            if (IsUserInLobby(Lobby, pParam->m_info.m_identityRemote.GetSteamID()))
                SteamNetworkingSockets()->AcceptConnection(pParam->m_hConn);
        }
        else
        {
           SteamNetworkingSockets()->AcceptConnection(pParam->m_hConn);
        }
        break;

    case k_ESteamNetworkingConnectionState_Connected:
        if (!CreateSteamConnectionTuple(pParam->m_hConn))
            SteamNetworkingSockets()->CloseConnection(pParam->m_hConn, k_ESteamNetConnectionEnd_App_PortsFilled, "", NULL);

        break;

    case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
        SteamNetworkingSockets()->CloseConnection(pParam->m_hConn, k_ESteamNetConnectionEnd_Misc_Timeout, "", NULL);
        break;
    }
}

CON_COMMAND_F(coplay_listinterfaces, "", FCVAR_CLIENTDLL)
{
    IPaddress addr[16];
    int num = SDLNet_GetLocalAddresses(addr, sizeof(addr) / sizeof(IPaddress));
    for (int i = 0; i < num; i++)
        Msg("%i.%i.%i.%i\n", ((uint8*)&addr[i].host)[0], ((uint8*)&addr[i].host)[1], ((uint8*)&addr[i].host)[2], ((uint8*)&addr[i].host)[3] );
}

void CCoplayConnectionHandler::LobbyCreated(LobbyCreated_t *pParam)
{
    Lobby = pParam->m_ulSteamIDLobby;
    char dir[512];
    V_snprintf(dir, sizeof(dir), "%s", engine->GetGameDirectory());
    CUtlVector<char*> split;
#if defined(LINUX) || defined(OSX)
    V_SplitString(dir, "/", split);
#else
    V_SplitString(dir, "\\", split);
#endif
    SteamMatchmaking()->SetLobbyData(Lobby, "gamedir", split.Tail());
    ConColorMsg(COPLAY_MSG_COLOR, "dir: %s", split.Tail());
}

void CCoplayConnectionHandler::LobbyJoined(LobbyEnter_t *pParam)
{
    SteamNetworkingIdentity netID;
    SteamNetworkingSockets()->GetIdentity(&netID);
    if (SteamMatchmaking()->GetLobbyOwner(pParam->m_ulSteamIDLobby) == netID.GetSteamID64())
        return; //Don't do anything extra if its ours

    SetRole(eConnectionRole_CLIENT);
    Lobby = pParam->m_ulSteamIDLobby;
    SteamNetworkingIdentity netIDRemote;
    netIDRemote.SetSteamID(SteamMatchmaking()->GetLobbyOwner(Lobby));
    SteamNetworkingConfigValue_t options[1];
    options[0].SetInt32(k_ESteamNetworkingConfig_EnableDiagnosticsUI, 1);
    SteamNetworkingSockets()->ConnectP2P(netIDRemote, 0, sizeof(options)/sizeof(SteamNetworkingConfigValue_t), options);
    ConColorMsg(COPLAY_MSG_COLOR, "Lobby owner: %llu\n", netIDRemote.GetSteamID64());
}

void CCoplayConnectionHandler::LobbyJoinRequested(GameLobbyJoinRequested_t *pParam)
{
    SteamMatchmaking()->JoinLobby(pParam->m_steamIDLobby);
}

CON_COMMAND(coplay_listlobbies, "")
{
    char dir[512];
    V_snprintf(dir, sizeof(dir), "%s", engine->GetGameDirectory());
    CUtlVector<char*> split;
#if defined(LINUX) || defined(OSX)
    V_SplitString(dir, "/", split);
#else
    V_SplitString(dir, "\\", split);
#endif

    SteamMatchmaking()->AddRequestLobbyListStringFilter("gamedir", split.Tail(), k_ELobbyComparisonEqual);
    SteamAPICall_t apiCall = SteamMatchmaking()->RequestLobbyList();
    g_pCoplayConnectionHandler->LobbyListResult.Set( apiCall, g_pCoplayConnectionHandler, &CCoplayConnectionHandler::OnLobbyListcmd);
}

void CCoplayConnectionHandler::OnLobbyListcmd(LobbyMatchList_t *pLobbyMatchList, bool bIOFailure)
{
    ConColorMsg(COPLAY_MSG_COLOR, "%i Available Lobbies:\n|    Name    |    Map    |    Game    |    ID    |\n", pLobbyMatchList->m_nLobbiesMatching);
    for (uint32 i = 0; i < pLobbyMatchList->m_nLobbiesMatching; i++)
    {
        CSteamID lobby = SteamMatchmaking()->GetLobbyByIndex(i);
        ConColorMsg(COPLAY_MSG_COLOR, "| %s | %s | %s | %llu |\n",
                    SteamMatchmaking()->GetLobbyData(lobby, "hostname"),
                    SteamMatchmaking()->GetLobbyData(lobby, "map"),
                    SteamMatchmaking()->GetLobbyData(lobby, "gamedir"),
                    lobby.ConvertToUint64()
                    );
    }
}


void CCoplayConnectionHandler::JoinGame(GameRichPresenceJoinRequested_t *pParam)
{
    char cmd[k_cchMaxRichPresenceValueLength];
    V_strcpy(cmd, pParam->m_rgchConnect);
    V_StrRight(cmd, -1, cmd, sizeof(cmd));//remove the + at the start
    engine->ClientCmd(cmd);
}

// CON_COMMAND(coplay_connect, "connect wrapper that adds coplay functionality")
// {
//     if (args.ArgC() < 1)
//         return;
//     char arg[128];
//     V_snprintf(arg, sizeof(arg), "%s", args.ArgS());
//     if(g_pCoplayConnectionHandler)
//     {
//         g_pCoplayConnectionHandler->CloseAllConnections();
//         g_pCoplayConnectionHandler->SetRole(eConnectionRole_CLIENT);
//     }

//     if (V_strstr(arg, "."))//normal server, probably
//     {
//         char cmd[64];
//         V_snprintf(cmd, sizeof(cmd), "connect %s", arg );
//         engine->ClientCmd(cmd);
//     }
//     else
//     {
//         SteamRelayNetworkStatus_t deets;
//         if ( SteamNetworkingUtils()->GetRelayNetworkStatus(&deets) != k_ESteamNetworkingAvailability_Current)
//         {
//             Warning("[Coplay Warning] Can't Connect! Connection to Steam Datagram Relay not yet established.\n");

//             //Game is probably just starting, queue the command to be run once the Steam network connection is established
//             int len = strlen(args.GetCommandString());
//             queuedcommand = (char*)malloc(len);
//             V_snprintf(queuedcommand, len, "%s", args.GetCommandString());
//             return;
//         }

//         ConColorMsg(COPLAY_MSG_COLOR, "[Coplay] Attempting Connection to lobby %s....\n", arg);
//         CSteamID steamid;
//         steamid.SetFromUint64(atoll(arg));
//         SteamMatchmaking()->JoinLobby(steamid);
//     }
// }

CON_COMMAND(connect_lobby, "")
{
    if (args.ArgC() < 1)
        return;
    char arg[128];
    V_snprintf(arg, sizeof(arg), "%s", args.ArgS());
    if (!g_pCoplayConnectionHandler)
    {
        Warning("[Coplay Warning] Can't Connect! Connection to Steam Datagram Relay not yet established.\n");

        //             //Game is probably just starting, queue the command to be run once the Steam network connection is established
        int len = strlen(args.GetCommandString());
        queuedcommand = (char*)malloc(len);
        V_snprintf(queuedcommand, len, "%s", args.GetCommandString());
        return;
    }
    ConColorMsg(COPLAY_MSG_COLOR, "[Coplay] Attempting Connection to lobby %s....\n", arg);
    CSteamID steamid;
    steamid.SetFromUint64(atoll(arg));
    SteamMatchmaking()->JoinLobby(steamid);
}
