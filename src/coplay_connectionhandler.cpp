
/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

//================================================
// CoaXioN Source SDK p2p networking: "CoaXioN Coplay"
// Author : Tholp / Jackson S
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
        if (coplay_connectionthread_hz.GetInt() > 0)
            g_pCoplayConnectionHandler->msSleepTime = min(2000, 1000/coplay_connectionthread_hz.GetInt());
        else
            g_pCoplayConnectionHandler->msSleepTime = 0;
    }
}
#ifdef COPLAY_USE_LOBBIES
static void ChangeLobbyType()
{
    if (g_pCoplayConnectionHandler && g_pCoplayConnectionHandler->GetLobby().IsLobby() && g_pCoplayConnectionHandler->GetLobby().IsValid())
        SteamMatchmaking()->SetLobbyType(g_pCoplayConnectionHandler->GetLobby(), (ELobbyType)coplay_joinfilter.GetInt());
}
#endif

ConVar coplay_joinfilter("coplay_joinfilter", "1", FCVAR_ARCHIVE, "Whos allowed to connect to our Game?\n"
                         "0  : Invite Only\n"
                         "1  : Friends Only\n"
                         "2  : Anyone\n",
                        true, 0, true, 2
#ifdef COPLAY_USE_LOBBIES
                        ,(FnChangeCallback_t)ChangeLobbyType // See the enum ELobbyType in isteammatchmaking.h
#endif
                         );
ConVar coplay_connectionthread_hz("coplay_connectionthread_hz", "300", FCVAR_ARCHIVE,
                                  "Number of times to run a connection per second. Only change this if you know what it means.\n", true, 1, false, 0, (FnChangeCallback_t)UpdateSleepTime);

ConVar coplay_debuglog_socketcreation("coplay_debuglog_socketcreation", "0", 0, "Prints more information when a socket is opened or closed.\n");
ConVar coplay_debuglog_steamconnstatus("coplay_debuglog_steamconnstatus", "0", 0, "Prints more detailed steam connection statuses.\n");
#ifdef COPLAY_USE_LOBBIES
ConVar coplay_debuglog_lobbyupdated("coplay_debuglog_lobbyupdated", "0", 0, "Prints when a lobby is created, joined or left.\n");
#endif

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
            return false;
        }
        SteamNetworkingUtils()->InitRelayNetworkAccess();

        ConColorMsg(COPLAY_MSG_COLOR, "[Coplay] Initialization started...\n");
        g_pCoplayConnectionHandler = new CCoplayConnectionHandler;
        return true;
    }
    virtual void PostInitAllSystems()
    {
        Remove(this);//No longer needed
    }
}coplaybootstrap;

void CCoplayConnectionHandler::Update(float frametime)
{
    static bool checkavail = true;
    static float lastSteamRPCUpdate = 0.0f;
    SteamAPI_RunCallbacks();
    if (checkavail) // The callback specifically to check this is registered by the engine already :/
    {
        SteamRelayNetworkStatus_t deets;
        if ( SteamNetworkingUtils()->GetRelayNetworkStatus(&deets) == k_ESteamNetworkingAvailability_Current)
        {
            UpdateSleepTime();
            checkavail = false;
            SetRole(eConnectionRole_NOT_CONNECTED);
            ConColorMsg(COPLAY_MSG_COLOR, "[Coplay] Steam Relay Connection successful!\n");
            ConColorMsg(COPLAY_MSG_COLOR, "[Coplay] Intialization Complete!\n");

            if (queuedcommand != NULL)
            {
                ConColorMsg(COPLAY_MSG_COLOR, "%s\n", queuedcommand);
                engine->ClientCmd_Unrestricted(queuedcommand);
                free(queuedcommand);
            }
        }
    }

    if (gpGlobals->realtime > lastSteamRPCUpdate + 1.0f)// Those poor steam servers...
    {
#ifndef COPLAY_USE_LOBBIES
        if (engine->IsConnected() && coplay_joinfilter.GetInt() != eP2PFilter_CONTROLLED)// Being in a lobby writes this for us
        {
            INetChannelInfo *netinfo = engine->GetNetChannelInfo();

            char szIP[32];
            if( (netinfo->IsLoopback() || V_strstr(netinfo->GetAddress(), "127.0.0.1") ) && HP2PSocket)
            {
                SteamNetworkingIdentity netID;
                if (SteamNetworkingSockets()->GetIdentity(&netID))
                {
                    V_snprintf(szIP, sizeof(szIP), "%llu", netID.GetSteamID64());
                }
                else
                {
                    lastSteamRPCUpdate = gpGlobals->realtime;
                    return;
                }
            }
            else if( netinfo )
            {
                V_snprintf(szIP, sizeof(szIP), "%s", netinfo->GetAddress());
            }
            else
            {
                SteamFriends()->SetRichPresence("connect", "");
                lastSteamRPCUpdate = gpGlobals->realtime;
                return;
            }
            char szConnectCMD[64] = "+coplay_connect ";
            V_strcat(szConnectCMD, szIP, sizeof(szConnectCMD));
            SteamFriends()->SetRichPresence("connect", szConnectCMD);
        }
#else
        if (Role == eConnectionRole_HOST && Lobby.IsLobby() && Lobby.IsValid())
        {
            ConVarRef hostname("hostname");
            char map[256];
            V_StrRight(engine->GetLevelName(), -5, map, sizeof(map));// remove "maps/"
            V_StrLeft(map, -4, map, sizeof(map));// remove ".bsp"
            SteamMatchmaking()->SetLobbyData(Lobby, "map", map);
            SteamMatchmaking()->SetLobbyData(Lobby, "hostname", hostname.GetString());
            SteamMatchmaking()->SetLobbyMemberLimit(Lobby, gpGlobals->maxClients);
        }
#endif
        lastSteamRPCUpdate = gpGlobals->realtime;
    }

#ifndef COPLAY_USE_LOBBIES //waiting for password on pending clients
    for (int i = 0; i < PendingConnections.Count(); i++)
    {
        SteamNetworkingMessage_t *InboundSteamMessages;
        if (!SteamNetworkingSockets()->ReceiveMessagesOnConnection(PendingConnections[i], &InboundSteamMessages, 1))
            return;
        char recvPassword[InboundSteamMessages->GetSize()];
        V_snprintf(recvPassword, InboundSteamMessages->GetSize(), "%s", (const char*)(InboundSteamMessages->GetData()));
        //Msg("Got password %s\n", recvPassword);
        if (!V_strcmp(Password, recvPassword))
        {
            CreateSteamConnectionTuple(PendingConnections[i]);
            SteamNetworkingSockets()->SendMessageToConnection(PendingConnections[i], COPLAY_NETMSG_OK, sizeof(COPLAY_NETMSG_OK), 0, NULL);
            PendingConnections.Remove(i); i--;
        }
        else
        {
            SteamNetworkingSockets()->CloseConnection(PendingConnections[i], k_ESteamNetConnectionEnd_App_BadPassword, "", true);
            PendingConnections.Remove(i); i--;
        }
    }
#endif
}
#ifndef COPLAY_USE_LOBBIES
void CCoplayConnectionHandler::RechoosePassword()
{
    char validchars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (int i = 0; i < sizeof(Password) - 1; i++)
        Password[i] = validchars[rand() % (sizeof(validchars) -1)];// -1 from size so the terminator isnt included
    Password[sizeof(Password)-1] = 0;
}

CON_COMMAND(coplay_rerandomize_password, "Randomizes the password given by coplay_getconnectcommand.\n")
{
    if (!g_pCoplayConnectionHandler || g_pCoplayConnectionHandler->GetRole() == eConnectionRole_NOT_CONNECTED)
    {
        ConColorMsg(COPLAY_MSG_COLOR, "You're not currently hosting a game.");
        return;
    }
    g_pCoplayConnectionHandler->RechoosePassword();
}

#endif

void CCoplayConnectionHandler::OpenP2PSocket()
{
    if (!engine->IsConnected())
    {
        ConColorMsg(COPLAY_MSG_COLOR, "You're not currently in a local game.");
        return;
    }
    INetChannelInfo *netinfo = engine->GetNetChannelInfo();
    if (!(netinfo->IsLoopback() || V_strstr(netinfo->GetAddress(), "127.0.0.1")))
    {
        ConColorMsg(COPLAY_MSG_COLOR, "You're not currently in a local game.%s\n", netinfo->GetAddress());
        return;
    }
    CloseP2PSocket();
    SetRole(eConnectionRole_HOST);
    HP2PSocket =  SteamNetworkingSockets()->CreateListenSocketP2P(0, 0, NULL);

#ifdef COPLAY_USE_LOBBIES
    ELobbyType lobbytype = (ELobbyType)coplay_joinfilter.GetInt();
    SteamMatchmaking()->CreateLobby(lobbytype, max(gpGlobals->maxClients, 2));//maxClients is 0 when disconnected (main menu)
#else
    RechoosePassword();
#endif
}

void CCoplayConnectionHandler::CloseP2PSocket()
{
    if (GetRole() != eConnectionRole_HOST)
        return;
    CloseAllConnections();
    SteamNetworkingSockets()->CloseListenSocket(HP2PSocket);
    HP2PSocket = 0;
#ifdef COPLAY_USE_LOBBIES
    SteamMatchmaking()->LeaveLobby(Lobby);
    Lobby.SetFromUint64( 0 );
#endif
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

CON_COMMAND(coplay_opensocket, "Open p2p listener")
{
    if (g_pCoplayConnectionHandler)
        g_pCoplayConnectionHandler->OpenP2PSocket();
}

CON_COMMAND(coplay_closesocket, "Close p2p listener")
{
    if (g_pCoplayConnectionHandler)
        g_pCoplayConnectionHandler->CloseP2PSocket();
}

CON_COMMAND(coplay_getconnectcommand, "Prints a command for other people to join you")
{
    if (!g_pCoplayConnectionHandler || g_pCoplayConnectionHandler->GetRole() == eConnectionRole_NOT_CONNECTED)
    {
        ConColorMsg(COPLAY_MSG_COLOR, "You're not currently in a game joinable by Coplay.\n");
        return;
    }

    uint64 id;
#ifdef COPLAY_USE_LOBBIES
    id = g_pCoplayConnectionHandler->GetLobby().ConvertToUint64();
#else
    SteamNetworkingIdentity netID;
    SteamNetworkingSockets()->GetIdentity(&netID);
    id = netID.GetSteamID64();
#endif

    if (coplay_joinfilter.GetInt() == eP2PFilter_CONTROLLED)
    {
#ifdef COPLAY_USE_LOBBIES
        ConColorMsg(COPLAY_MSG_COLOR, "You currently have coplay_joinfilter set to invite only, Use coplay_invite\n");
#else
        ConColorMsg(COPLAY_MSG_COLOR, "\ncoplay_connect %llu %s", id, g_pCoplayConnectionHandler->GetPassword());
#endif
    }
    else
        ConColorMsg(COPLAY_MSG_COLOR, "\ncoplay_connect %llu\n", id);
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

    return true;
}

void CCoplayConnectionHandler::SetRole(ConnectionRole newrole)
{
    ConVarRef sv_lan("sv_lan");
    ConVarRef engine_no_focus_sleep("engine_no_focus_sleep");
    switch (newrole)
    {
    case eConnectionRole_HOST:
        sv_lan.SetValue("1");//sv_lan off will heartbeat the server and allow clients to see our ip
        engine_no_focus_sleep.SetValue("0"); // without this, if the host tabs out everyone lags
        break;
    case eConnectionRole_CLIENT:
        engine_no_focus_sleep.SetValue(engine_no_focus_sleep.GetDefault());
        break;
    case eConnectionRole_NOT_CONNECTED:
        CloseAllConnections();
    }


    Role = newrole;
}

CON_COMMAND_F(coplay_debug_createdummyconnection, "Create a empty connection", FCVAR_DEVELOPMENTONLY)
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
#ifdef COPLAY_USE_LOBBIES

            if (IsUserInLobby(Lobby, pParam->m_info.m_identityRemote.GetSteamID()))
                SteamNetworkingSockets()->AcceptConnection(pParam->m_hConn);

#else
            switch (coplay_joinfilter.GetInt())
            {
            case eP2PFilter_EVERYONE:
                    SteamNetworkingSockets()->AcceptConnection(pParam->m_hConn);
                    break;
            case eP2PFilter_FRIENDS:
                if (SteamFriends()->HasFriend(ID.GetSteamID(), k_EFriendFlagImmediate))
                    SteamNetworkingSockets()->AcceptConnection(pParam->m_hConn);
                else
                    SteamNetworkingSockets()->CloseConnection(pParam->m_hConn, k_ESteamNetConnectionEnd_App_NotFriend, "", true);
                break;
            case eP2PFilter_CONTROLLED: //This is passwords, we cant get the password before making a connection so dont make a CCoplayConnection until we get it
                // Connections in PendingConnections are run in CCoplayConnectionHandler::Update() waiting to recieve it
                    SteamNetworkingSockets()->AcceptConnection(pParam->m_hConn);
                    PendingConnections.AddToTail(pParam->m_hConn);
                break;


            default:
                SteamNetworkingSockets()->CloseConnection(pParam->m_hConn, k_ESteamNetConnectionEnd_App_NotOpen, "", true);
                break;
            }

#endif
        }
        else
        {
           SteamNetworkingSockets()->AcceptConnection(pParam->m_hConn);
        }
        break;

    case k_ESteamNetworkingConnectionState_Connected:
#ifndef COPLAY_USE_LOBBIES
        if (GetRole() == eConnectionRole_HOST)
        {
            for (int i = 0; i < PendingConnections.Count(); i++)
                if (PendingConnections[i] == pParam->m_hConn)
                {
                    //send a message that we're expecting a password
                    SteamNetworkingSockets()->SendMessageToConnection(pParam->m_hConn, COPLAY_NETMSG_NEEDPASS, sizeof(COPLAY_NETMSG_NEEDPASS),
                                                                      k_nSteamNetworkingSend_Reliable, NULL);
                    return;
                }

            SteamNetworkingSockets()->SendMessageToConnection(pParam->m_hConn, COPLAY_NETMSG_OK, sizeof(COPLAY_NETMSG_OK),
                                                              k_nSteamNetworkingSend_Reliable, NULL);
        }
#endif

        if (!CreateSteamConnectionTuple(pParam->m_hConn))
        {
            SteamNetworkingSockets()->CloseConnection(pParam->m_hConn, k_ESteamNetConnectionEnd_App_RemoteIssue, "", true);
#ifdef COPLAY_USE_LOBBIES
            SteamMatchmaking()->LeaveLobby(Lobby);
#endif
        }

        break;

    case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
        SteamNetworkingSockets()->CloseConnection(pParam->m_hConn, k_ESteamNetConnectionEnd_Misc_Timeout, "", true);
#ifdef COPLAY_USE_LOBBIES
        SteamMatchmaking()->LeaveLobby(Lobby);
#else
        for (int i = 0; i < PendingConnections.Count(); i++)
            if (PendingConnections[i] == pParam->m_hConn)
            {
                PendingConnections.Remove(i);
                return;
            }
#endif
        break;
    }
}

#ifdef COPLAY_USE_LOBBIES
void CCoplayConnectionHandler::LobbyCreated(LobbyCreated_t *pParam)
{
    SteamMatchmaking()->LeaveLobby(Lobby);//Leave the old lobby if we were in one
    Lobby = pParam->m_ulSteamIDLobby;
}

void CCoplayConnectionHandler::LobbyJoined(LobbyEnter_t *pParam)
{
    if (pParam->m_EChatRoomEnterResponse != 1)
    {
        ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "[Coplay] Failed to Join lobby! Errorcode: %i\n",pParam->m_EChatRoomEnterResponse);
        return;// TODO proper messages and maybe a function pointer for mods
    }
    SteamNetworkingIdentity netID;
    SteamNetworkingSockets()->GetIdentity(&netID);
    ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "Lobby Joined\n");
    ConColorMsg(COPLAY_MSG_COLOR, "Lobby owner: %llu\n", SteamMatchmaking()->GetLobbyOwner(pParam->m_ulSteamIDLobby).ConvertToUint64());

    if (SteamMatchmaking()->GetLobbyOwner(pParam->m_ulSteamIDLobby) == netID.GetSteamID64())
        return; //Don't do anything extra if its ours

    SetRole(eConnectionRole_CLIENT);
    SteamMatchmaking()->LeaveLobby(Lobby);//Leave the old lobby if we were in one
    Lobby = pParam->m_ulSteamIDLobby;

    char cmd[64];
    V_snprintf(cmd, sizeof(cmd), "coplay_connect %llu", SteamMatchmaking()->GetLobbyOwner(Lobby).ConvertToUint64());
    engine->ClientCmd_Unrestricted(cmd);//Just use the same code

}

void CCoplayConnectionHandler::LobbyJoinRequested(GameLobbyJoinRequested_t *pParam)
{
    SteamMatchmaking()->JoinLobby(pParam->m_steamIDLobby);
}

CON_COMMAND(coplay_listlobbies, "List all joinable lobbies")
{
    SteamAPICall_t apiCall = SteamMatchmaking()->RequestLobbyList();
    g_pCoplayConnectionHandler->LobbyListResult.Set( apiCall, g_pCoplayConnectionHandler, &CCoplayConnectionHandler::OnLobbyListcmd);
}

void CCoplayConnectionHandler::OnLobbyListcmd(LobbyMatchList_t *pLobbyMatchList, bool bIOFailure)
{
    if (bIOFailure)
    {
        ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "%s::%i \"IOFailure\"!\n", __FILE__, __LINE__);
        return;
    }
    ConColorMsg(COPLAY_MSG_COLOR, "%i Available Lobbies:\n|    Name    |    Map    |    Players    |    ID    |\n", pLobbyMatchList->m_nLobbiesMatching);
    for (uint32 i = 0; i < pLobbyMatchList->m_nLobbiesMatching; i++)
    {
        CSteamID lobby = SteamMatchmaking()->GetLobbyByIndex(i);
        ConColorMsg(COPLAY_MSG_COLOR, "| %s | %s | %i/%i | %llu |\n",
                    SteamMatchmaking()->GetLobbyData(lobby, "hostname"),
                    SteamMatchmaking()->GetLobbyData(lobby, "map"),
                    SteamMatchmaking()->GetNumLobbyMembers(lobby),
                    SteamMatchmaking()->GetLobbyMemberLimit(lobby),
                    lobby.ConvertToUint64()
                    );
    }
}
#endif

void CCoplayConnectionHandler::JoinGame(GameRichPresenceJoinRequested_t *pParam)
{
    char cmd[k_cchMaxRichPresenceValueLength];
    V_strcpy(cmd, pParam->m_rgchConnect);
    V_StrRight(cmd, -1, cmd, sizeof(cmd));// Remove the + at the start
    engine->ClientCmd_Unrestricted(cmd);
}


CON_COMMAND(coplay_connect, "Connect wrapper that adds coplay functionality, use with an IP or SteamID64.")
{
    if (args.ArgC() < 1)
        return;

    if (atoll(args.Arg(1)) <= 0)
    {
        Warning("No connect target given.\n");
        return;
    }

    if(!g_pCoplayConnectionHandler)
    {
        if (queuedcommand)// We're not ready to handle this yet
        {
            free(queuedcommand);
            queuedcommand = NULL;
        }
        int len = strlen(args.GetCommandString());
        queuedcommand = (char*)malloc(len);
        V_snprintf(queuedcommand, len, "%s", args.GetCommandString());
        return;
    }

    char Id[64];
    V_snprintf(Id, sizeof(Id), "%s", args.Arg(1));
#ifndef COPLAY_USE_LOBBIES
    //Store it here for when we might need to send it later
    V_snprintf(g_pCoplayConnectionHandler->Password, sizeof(g_pCoplayConnectionHandler->Password), "%s", args.Arg(2));
#endif

    g_pCoplayConnectionHandler->CloseAllConnections();
    g_pCoplayConnectionHandler->SetRole(eConnectionRole_CLIENT);

    if (V_strstr(Id, "."))//normal server, probably
    {
        char cmd[64];
        V_snprintf(cmd, sizeof(cmd), "connect %s", Id );
        engine->ClientCmd_Unrestricted(cmd);
    }
    else // what you're here for
    {
        SteamRelayNetworkStatus_t deets;
        if ( SteamNetworkingUtils()->GetRelayNetworkStatus(&deets) != k_ESteamNetworkingAvailability_Current)
        {
            Warning("[Coplay Warning] Can't Connect! Connection to Steam Datagram Relay not yet established.\n");

            if (queuedcommand)// We're not ready to handle this yet
            {
                free(queuedcommand);
                queuedcommand = NULL;
            }
            // Game is probably just starting, queue the command to be run once the Steam network connection is established
            int len = strlen(args.GetCommandString()) + 1;// Remember to have space for the terminator kids...
            queuedcommand = (char*)malloc(len);
            V_snprintf(queuedcommand, len, "%s", args.GetCommandString());
            return;
        }
        CSteamID steamid;
        steamid.SetFromUint64(atoll(Id));
#ifdef COPLAY_USE_LOBBIES
        if (steamid.IsLobby())
        {
            ConColorMsg(COPLAY_MSG_COLOR, "[Coplay] Attempting to join lobby with ID %s....\n", Id);
            SteamMatchmaking()->JoinLobby(steamid);
            return;
        }
#endif
        if (steamid.BIndividualAccount())
        {
#ifdef COPLAY_USE_LOBBIES
            if (g_pCoplayConnectionHandler->GetLobby().ConvertToUint64() == 0)
            {
                Warning("Coplay_Connect was called with a user SteamID before we were in a lobby!\n");
                return;
            }
#endif
            SteamNetworkingIdentity netID;
            netID.SetSteamID64(atoll(Id));

            SteamNetworkingConfigValue_t options[1];
            options[0].SetInt32(k_ESteamNetworkingConfig_EnableDiagnosticsUI, 1);

            ConColorMsg(COPLAY_MSG_COLOR, "[Coplay] Attempting Connection to user with ID %llu....\n", netID.GetSteamID64());
            SteamNetworkingSockets()->ConnectP2P(netID, 0, sizeof(options)/sizeof(SteamNetworkingConfigValue_t), options);
            return;
        }
        Warning("Coplay_Connect was called with an invalid SteamID! (%llu)\n", steamid.ConvertToUint64());
    }
}
#ifdef COPLAY_USE_LOBBIES
CON_COMMAND_F(connect_lobby, "", FCVAR_HIDDEN )// Steam appends '+connect_lobby (id64)' to launch options when boot joining
{
    char cmd[128];
    V_snprintf(cmd, sizeof(cmd), "coplay_connect %s", args.ArgS());
    engine->ClientCmd_Unrestricted(cmd);

}

CON_COMMAND(coplay_invite, "")
{
    if (!g_pCoplayConnectionHandler)
        return;
    if (g_pCoplayConnectionHandler->GetLobby().ConvertToUint64() == 0)
    {
        ConColorMsg(COPLAY_MSG_COLOR, "You aren't in a lobby.\n");
        return;
    }
    SteamFriends()->ActivateGameOverlayInviteDialog(g_pCoplayConnectionHandler->GetLobby());
}
#else



#endif
