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
#include <vgui/ISystem.h>
#include <tier3/tier3.h>

std::string queuedcommand;


static void UpdateSleepTime()
{
    if (g_pCoplayConnectionHandler)
        g_pCoplayConnectionHandler->msSleepTime = 1000/coplay_connectionthread_hz.GetInt();  
}

#ifdef COPLAY_USE_LOBBIES
static void ChangeLobbyType()
{
    int filter = coplay_joinfilter.GetInt();
    if (g_pCoplayConnectionHandler && g_pCoplayConnectionHandler->GetLobby().IsLobby() && g_pCoplayConnectionHandler->GetLobby().IsValid())
        SteamMatchmaking()->SetLobbyType(g_pCoplayConnectionHandler->GetLobby(),
                                         (ELobbyType)( filter > -1 ? filter : 0));
}
#endif

ConVar coplay_joinfilter("coplay_joinfilter", "-1", FCVAR_ARCHIVE, "Whos allowed to connect to our Game? Will also call coplay_opensocket on server start if set above -1.\n"
                        "-1 : Off\n"
                        "0  : Invite Only\n"
                        "1  : Friends Only\n"
                        "2  : Anyone\n",
                        true, -1, true, 2
#ifdef COPLAY_USE_LOBBIES
                        ,(FnChangeCallback_t)ChangeLobbyType // See the enum ELobbyType in isteammatchmaking.h
#endif
                         );
ConVar coplay_connectionthread_hz("coplay_connectionthread_hz", "300", FCVAR_ARCHIVE,
                                  "Number of times to run a connection per second. Only change this if you know what it means.\n",
                                  true, 10, false, 0, (FnChangeCallback_t)UpdateSleepTime);

ConVar coplay_debuglog_socketcreation("coplay_debuglog_socketcreation", "0", 0, "Prints more information when a socket is opened or closed.\n");
ConVar coplay_debuglog_steamconnstatus("coplay_debuglog_steamconnstatus", "0", 0, "Prints more detailed steam connection statuses.\n");
#ifdef COPLAY_USE_LOBBIES
ConVar coplay_debuglog_lobbyupdated("coplay_debuglog_lobbyupdated", "0", 0, "Prints when a lobby is created, joined or left.\n");
#endif

CCoplayConnectionHandler *g_pCoplayConnectionHandler;
CCoplayConnectionHandler CoplayConnectionHandler;

void CCoplayConnectionHandler::Update(float frametime)
{
    static bool checkavail = true;
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

            if (!queuedcommand.empty())
            {
                ConColorMsg(COPLAY_MSG_COLOR, "%s\n", queuedcommand.c_str());
                engine->ClientCmd_Unrestricted(queuedcommand.c_str());
            }
        }
    }

#ifndef COPLAY_DONT_UPDATE_RPC
    static float lastSteamRPCUpdate = 0.0f;
    if (gpGlobals->realtime > lastSteamRPCUpdate + 1.0f)
    {
#ifndef COPLAY_USE_LOBBIES
        if (engine->IsConnected() && coplay_joinfilter.GetInt() != eP2PFilter_CONTROLLED)// Being in a lobby sets the connect string automatically
        {
            INetChannelInfo *netinfo = engine->GetNetChannelInfo();

            std::string IP = netinfo->GetAddress();
            if( (netinfo->IsLoopback() || IP.find("127") == 0 ) && HP2PSocket)
            {
                SteamNetworkingIdentity netID;
                if (SteamNetworkingSockets()->GetIdentity(&netID))
                {
                    IP = std::to_string(netID.GetSteamID64());
                }
                else
                {
                    lastSteamRPCUpdate = gpGlobals->realtime;
                    return;
                }
            }
            else if( netinfo )
            {
                IP = netinfo->GetAddress();
            }
            else
            {
                SteamFriends()->SetRichPresence("connect", "");
                lastSteamRPCUpdate = gpGlobals->realtime;
                return;
            }
            std::string szConnectCMD = "+coplay_connect " + IP;
            SteamFriends()->SetRichPresence("connect", szConnectCMD.c_str());
        }
#else
        if (Role == eConnectionRole_HOST && Lobby.IsLobby() && Lobby.IsValid())
        {
            ConVarRef hostname("hostname");
            std::string map(engine->GetLevelName());
            if (map.length() > std::string("maps/x.bsp").length())//check the map name isnt unreasonably short
            {
                map.erase(0,5);// remove "maps/"
                map.erase( map.length()-4,4);// remove ".bsp"
                SteamMatchmaking()->SetLobbyData(Lobby, "map", map.c_str());
            }
            SteamMatchmaking()->SetLobbyData(Lobby, "hostname", hostname.GetString());
            SteamMatchmaking()->SetLobbyMemberLimit(Lobby, gpGlobals->maxClients);
        }
#endif
        lastSteamRPCUpdate = gpGlobals->realtime;
    }
#endif

#ifndef COPLAY_USE_LOBBIES //waiting for password on pending clients
    for (int i = 0; i < PendingConnections.Count(); i++)
    {
        //Timeout
        if (PendingConnections[i].TimeCreated + coplay_timeoutduration.GetFloat() < gpGlobals->realtime)
        {
            SteamNetworkingSockets()->CloseConnection(PendingConnections[i].SteamConnection, k_ESteamNetConnectionEnd_App_ClosedByPeer, "", false);
            PendingConnections.Remove(i); i--;//decrement i because removing one from the vector shifts it all
        }

        //check for responses
        SteamNetworkingMessage_t *InboundSteamMessage;
        if (SteamNetworkingSockets()->ReceiveMessagesOnConnection(PendingConnections[i].SteamConnection, &InboundSteamMessage, 1))
        {
            std::string recvPassword((const char*)(InboundSteamMessage->GetData()), InboundSteamMessage->GetSize());
            Msg("Got password %s\n", recvPassword.c_str());
            if (Password == recvPassword)
            {
                CreateSteamConnectionTuple(PendingConnections[i].SteamConnection);
                SteamNetworkingSockets()->SendMessageToConnection(PendingConnections[i].SteamConnection,
                                                                  COPLAY_NETMSG_OK, sizeof(COPLAY_NETMSG_OK), k_nSteamNetworkingSend_ReliableNoNagle, NULL);
                PendingConnections.Remove(i); i--;
            }
            else
            {
                SteamNetworkingSockets()->CloseConnection(PendingConnections[i].SteamConnection, k_ESteamNetConnectionEnd_App_BadPassword, "", false);
                PendingConnections.Remove(i); i--;
            }
        }
    }
#endif
}


void CCoplayConnectionHandler::LevelInitPostEntity()//Open p2p automatically
{
    if (GetRole() != eConnectionRole_NOT_CONNECTED || coplay_joinfilter.GetInt() == eP2PFilter_OFF)
        return;

    INetChannelInfo *netinfo = engine->GetNetChannelInfo();
    std::string ip = netinfo->GetAddress();
    if (!(netinfo->IsLoopback() || ip.find("127") == 0))// check its our game and not a dedicated
        return;

    OpenP2PSocket();
}

void CCoplayConnectionHandler::LevelShutdownPreEntity()
{
    if (!engine->IsConnected())//server disconnect/shutdown
    {
        CloseAllConnections();
        SetRole(eConnectionRole_NOT_CONNECTED);
    }

}

#ifndef COPLAY_USE_LOBBIES
void CCoplayConnectionHandler::RechoosePassword()
{
    static const std::string validchars= "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    Password.clear();
    for (int i = 0; i < 32; i++)
         Password += validchars[rand() % validchars.length()];
}

CON_COMMAND(coplay_rerandomize_password, "Randomizes the password given by coplay_getconnectcommand.\n")
{
    if (!g_pCoplayConnectionHandler || g_pCoplayConnectionHandler->GetRole() != eConnectionRole_HOST)
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
    std::string ip = netinfo->GetAddress();
    if (!(netinfo->IsLoopback() || ip.find("127") == 0))
    {
        ConColorMsg(COPLAY_MSG_COLOR, "You're not currently in a local game.%s\n", netinfo->GetAddress());
        return;
    }
    CloseP2PSocket();
    CloseAllConnections();
    SetRole(eConnectionRole_HOST);
    HP2PSocket =  SteamNetworkingSockets()->CreateListenSocketP2P(0, 0, NULL);

#ifdef COPLAY_USE_LOBBIES
    SteamMatchmaking()->LeaveLobby(Lobby);
    int filter = coplay_joinfilter.GetInt();
    ELobbyType lobbytype = (ELobbyType)( filter > -1 ? filter : 0);
    SteamMatchmaking()->CreateLobby(lobbytype, gpGlobals->maxClients);
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

CON_COMMAND(coplay_opensocket, "Manually (re)open your game to P2P connections")
{
    if (g_pCoplayConnectionHandler)
        g_pCoplayConnectionHandler->OpenP2PSocket();
}

CON_COMMAND(coplay_closesocket, "Close p2p listener")
{
    if (g_pCoplayConnectionHandler)
        g_pCoplayConnectionHandler->CloseP2PSocket();
}

int CCoplayConnectionHandler::GetConnectCommand(std::string &out)
{
    out = "";
    if ( GetRole() == eConnectionRole_NOT_CONNECTED )
    {
        return 1;
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
        return 2;
#else
        out = "coplay_connect " + std::to_string(id) + " " + GetPassword();
#endif
    }
    else
    {
        out = "coplay_connect " + std::to_string(id);
    }

    return 0;
}

CON_COMMAND(coplay_getconnectcommand, "Prints a command for other people to join you")
{
    if (!g_pCoplayConnectionHandler)
        return;
    std::string cmd;
    switch (g_pCoplayConnectionHandler->GetConnectCommand(cmd))
    {
    case 1:
        ConColorMsg(COPLAY_MSG_COLOR, "You're not currently in a game joinable by Coplay.\n");
    break;

    case 2:
        ConColorMsg(COPLAY_MSG_COLOR, "You currently have coplay_joinfilter set to invite only, Use coplay_invite\n");
    break;

    case 0:
        ConColorMsg(COPLAY_MSG_COLOR, "\n%s\nCopied to clipboard.", cmd.c_str());
        g_pVGuiSystem->SetClipboardText(cmd.c_str(), cmd.length());
    break;
    }
}

bool CCoplayConnectionHandler::CreateSteamConnectionTuple(HSteamNetConnection hConn)
{
    SteamNetConnectionInfo_t newinfo;
    if (!SteamNetworkingSockets()->GetConnectionInfo(hConn, &newinfo))
        return false;

    for (int i = 0; i< Connections.Count(); i++)
    {
        SteamNetConnectionInfo_t info;
        if (SteamNetworkingSockets()->GetConnectionInfo(Connections[i]->SteamConnection, &info))
        {
            if (info.m_identityRemote.GetSteamID64() == newinfo.m_identityRemote.GetSteamID64())
            {
                Connections[i]->QueueForDeletion();
                break;
            }
        }
    }


    CCoplayConnection *connection = new CCoplayConnection(hConn);

    connection->Start();
    Connections.AddToTail(connection);


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
#ifdef COPLAY_USE_LOBBIES
        SteamMatchmaking()->LeaveLobby(Lobby);
        Lobby.SetFromUint64( 0 );
#endif
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

    if (GetRole() == eConnectionRole_HOST && !engine->IsConnected())// Somehow left without us catching it, map transistion load error or cancelation probably
    {
        SetRole(eConnectionRole_NOT_CONNECTED);
        SteamNetworkingSockets()->CloseConnection(pParam->m_hConn, k_ESteamNetConnectionEnd_App_NotOpen, "", false);
        return;
    }


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

            case eP2PFilter_CONTROLLED:
            //This is for passwords, we cant get the password before making a connection so dont make a CCoplayConnection until we get it
            // Connections in PendingConnections are run in CCoplayConnectionHandler::Update() waiting to recieve it
            {
                SteamNetworkingSockets()->AcceptConnection(pParam->m_hConn);
                PendingConnection newPending;
                newPending.SteamConnection = pParam->m_hConn;
                newPending.TimeCreated = gpGlobals->realtime;
                PendingConnections.AddToTail(newPending);
            }
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
                if (PendingConnections[i].SteamConnection == pParam->m_hConn)
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
            if (GetRole() != eConnectionRole_HOST)
                SteamMatchmaking()->LeaveLobby(Lobby);
#endif
        }

        break;

    case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
        SteamNetworkingSockets()->CloseConnection(pParam->m_hConn, k_ESteamNetConnectionEnd_Misc_Timeout, "", true);
    // Intentionally no break
    case k_ESteamNetworkingConnectionState_ClosedByPeer:

#ifdef COPLAY_USE_LOBBIES
        if (GetRole() != eConnectionRole_HOST)
            SteamMatchmaking()->LeaveLobby(Lobby);
#else
        for (int i = 0; i < PendingConnections.Count(); i++)
        {
            if (PendingConnections[i].SteamConnection == pParam->m_hConn)
            {
                PendingConnections.Remove(i);
                return;
            }
        }
#endif
        for (int i = 0; i < Connections.Count(); i++)
        {
            if (Connections[i]->SteamConnection == pParam->m_hConn)
                Connections[i]->QueueForDeletion();
        }


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

    std::string cmd = "coplay_connect " + std::to_string(SteamMatchmaking()->GetLobbyOwner(Lobby).ConvertToUint64());
    engine->ClientCmd_Unrestricted(cmd.c_str());//Just use the same code

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
    if (!pParam || !pParam->m_rgchConnect)
        return;

    std::string cmd = pParam->m_rgchConnect;

    if(cmd.empty())
        goto badinput;

    if (cmd.length() > 70)
        goto badinput;

    if (std::string("+coplay_connect ") != cmd.substr(0, 16))
        goto badinput;

    if (cmd.find_first_of("\'\"\\/;", 0) != std::string::npos)//no quotation marks, slashes or semicolons
        goto badinput;

    cmd.erase(0,1);//remove the plus thats needed for boot joining

    engine->ClientCmd_Unrestricted(cmd.c_str());
    return;
badinput:
    ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "[Coplay Warning] CCoplayConnectionHandler::JoinGame() Was given a bad/invalid/empty command ( %s )!"
                                        "Make sure both parties are updated and that you trust the host you're trying to connect to.\n", cmd.c_str());
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
        //we're not ready to handle this yet
        queuedcommand = std::string(args.GetCommandString());
        return;
    }

    std::string Id = args.Arg(1);
#ifndef COPLAY_USE_LOBBIES
    //Store it here for when we might need to send it later
    g_pCoplayConnectionHandler->Password = std::string(args.Arg(2));
#endif

    g_pCoplayConnectionHandler->CloseAllConnections();


    if ( Id.find_first_of('.', 0) != std::string::npos )//normal server, probably
    {
        std::string cmd = "connect " + Id;
        engine->ClientCmd_Unrestricted(cmd.c_str());
    }
    else // what you're here for
    {
        SteamRelayNetworkStatus_t deets;
        if ( SteamNetworkingUtils()->GetRelayNetworkStatus(&deets) != k_ESteamNetworkingAvailability_Current)
        {
            Warning("[Coplay Warning] Can't Connect! Connection to Steam Datagram Relay not yet established.\n");

            // Game is probably just starting, queue the command to be run once the Steam network connection is established
            queuedcommand = std::string(args.GetCommandString());
            return;
        }
        if (engine->IsConnected())
        {
            engine->ClientCmd_Unrestricted("disconnect");//mimic normal connect behavior
        }

        CSteamID steamid;

        steamid.SetFromUint64(strtoull(Id.c_str(), NULL, 10));
#ifdef COPLAY_USE_LOBBIES
        if (steamid.IsLobby())
        {
            ConColorMsg(COPLAY_MSG_COLOR, "[Coplay] Attempting to join lobby with ID %s....\n", Id.c_str());
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
            netID.SetSteamID64(strtoull(Id.c_str(), NULL, 10));

            ConColorMsg(COPLAY_MSG_COLOR, "[Coplay] Attempting Connection to user with ID %llu....\n", netID.GetSteamID64());
            g_pCoplayConnectionHandler->SetRole(eConnectionRole_CLIENT);
            SteamNetworkingSockets()->ConnectP2P(netID, 0, 0, NULL);
            return;
        }
        Warning("Coplay_Connect was called with an invalid SteamID! ( %llu )\n", steamid.ConvertToUint64());
    }
}
#ifdef COPLAY_USE_LOBBIES
CON_COMMAND_F(connect_lobby, "", FCVAR_HIDDEN )// Steam appends '+connect_lobby (id64)' to launch options when boot joining
{
    std::string cmd = "coplay_connect ";
    cmd += args.ArgS();
    engine->ClientCmd_Unrestricted(cmd.c_str());

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
#endif

CON_COMMAND(coplay_debug_printstate, "")
{
    if (!g_pCoplayConnectionHandler)
        return;

    ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "Role: %i\nActive Connections: %i\n", g_pCoplayConnectionHandler->GetRole(), g_pCoplayConnectionHandler->Connections.Count());
}

CON_COMMAND(coplay_about, "")
{
    ConColorMsg(COPLAY_MSG_COLOR, "Coplay allows P2P connections in sourcemods. Visit the Github page for more information and source code\n");
    ConColorMsg(COPLAY_MSG_COLOR, "https://github.com/CoaXioN-Games/coplay\n\n");
    ConColorMsg(COPLAY_MSG_COLOR, "The loaded Coplay version is %s.\n\n", COPLAY_VERSION);

    ConColorMsg(COPLAY_MSG_COLOR, "Active Coplay build options:\n");
#ifdef COPLAY_USE_LOBBIES
    ConColorMsg(COPLAY_MSG_COLOR, " - COPLAY_USE_LOBBIES\n");
#endif
#ifdef COPLAY_DONT_UPDATE_RPC
    ConColorMsg(COPLAY_MSG_COLOR, " - COPLAY_DONT_UPDATE_RPC\n");
#endif
#ifdef COPLAY_DONT_LINK_SDL2
    ConColorMsg(COPLAY_MSG_COLOR, " - COPLAY_DONT_LINK_SDL2\n");
#endif
#ifdef COPLAY_DONT_LINK_SDL2_NET
    ConColorMsg(COPLAY_MSG_COLOR, " - COPLAY_DONT_LINK_SDL2_NET\n");
#endif
#ifdef COPLAY_DONT_SET_THREADMODE
    ConColorMsg(COPLAY_MSG_COLOR, " - COPLAY_DONT_SET_THREADMODE\n");
#endif
}
