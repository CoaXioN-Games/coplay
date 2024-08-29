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
#include "coplay_system.h"
#include <inetchannel.h>
#include <inetchannelinfo.h>
#include <steam/isteamgameserver.h>
#include <vgui/ISystem.h>
#include <tier3/tier3.h>



std::string queuedcommand;
static CCoplaySystem g_CoplaySystem;
CCoplaySystem* CCoplaySystem::s_instance = nullptr;


void ChangeLobbyType(IConVar* var, const char* pOldValue, float flOldValue)
{
    ConVarRef joinfilter(var);
    if (!joinfilter.IsValid())
        return;

    int filter = joinfilter.GetInt();
	CCoplaySystem* pCoplaySystem = CCoplaySystem::GetInstance();

    if (pCoplaySystem && pCoplaySystem->GetLobby().IsLobby() && pCoplaySystem->GetLobby().IsValid())
        SteamMatchmaking()->SetLobbyType(pCoplaySystem->GetLobby(),
            (ELobbyType)(filter > -1 ? filter : 0));
}

ConVar coplay_joinfilter("coplay_joinfilter", "-1", FCVAR_ARCHIVE, "Whos allowed to connect to our Game? Will also call coplay_opensocket on server start if set above -1.\n"
                        "-1 : Off\n"
                        "0  : Invite Only\n"
                        "1  : Friends Only\n"
                        "2  : Anyone\n",
                        true, -1, true, 2
                        ,(FnChangeCallback_t)ChangeLobbyType // See the enum ELobbyType in isteammatchmaking.h
                         );

ConVar coplay_debuglog_steamconnstatus("coplay_debuglog_steamconnstatus", "0", 0, "Prints more detailed steam connection statuses.\n");
ConVar coplay_debuglog_lobbyupdated("coplay_debuglog_lobbyupdated", "0", 0, "Prints when a lobby is created, joined or left.\n");
ConVar coplay_use_lobbies("coplay_use_lobbies", "1", 0, "Use Steam Lobbies for connections.\n");

CCoplaySystem::CCoplaySystem() : CAutoGameSystemPerFrame("CoplaySystem")
{
	m_oldConnectCallback = NULL;
	s_instance = this;
}

CCoplaySystem* CCoplaySystem::GetInstance()
{
	return s_instance;
}

bool CCoplaySystem::Init()
{
    ConColorMsg(COPLAY_MSG_COLOR, "[Coplay] Initialization started...\n");

#ifdef GAME_DLL //may see if we can support dedicated servers at some point
    if (!engine->IsDedicatedServer())
    {
        Remove(this);
        return true;
    }
#endif

    if (SDL_Init(0))
    {
        Error("SDL Failed to Initialize: \"%s\"", SDL_GetError());
    }
    if (SDLNet_Init())
    {
        Error("SDLNet Failed to Initialize: \"%s\"", SDLNet_GetError());
    }

    SteamNetworkingUtils()->InitRelayNetworkAccess();
    return true;
}

void CCoplaySystem::Shutdown()
{
    CloseAllConnections(true);
}

static void ConnectOverride(const CCommand& args)
{
    CCoplaySystem::GetInstance()->CoplayConnect(args);
}

void CCoplaySystem::PostInit()
{
    // Some cvars we need on
    ConVarRef net_usesocketsforloopback("net_usesocketsforloopback");// allows connecting to 127.* addresses
    net_usesocketsforloopback.SetValue(true);
#ifndef COPLAY_DONT_SET_THREADMODE
    ConVarRef host_thread_mode("host_thread_mode");// fixes game logic speedup, see the README for the required fix for this
    host_thread_mode.SetValue(2);
#endif

	// replace the connect command with our own
    ConCommand* connectCommand = g_pCVar->FindCommand("connect");
    if (!connectCommand)
        return;

	// vtable member variable offset magic
	// this offset should be the same on the SP, MP and Alien Swarm branches. If you're on something older sorry.
    m_oldConnectCallback = *(FnCommandCallback_t*)((intptr_t)(connectCommand)+0x18);
    *(FnCommandCallback_t*)((intptr_t)(connectCommand)+0x18) = ConnectOverride;
}

void CCoplaySystem::Update(float frametime)
{
    static bool checkavail = true;
    SteamAPI_RunCallbacks();
    if (checkavail) // The callback specifically to check this is registered by the engine already :/
    {
        SteamRelayNetworkStatus_t deets;
        if (SteamNetworkingUtils()->GetRelayNetworkStatus(&deets) == k_ESteamNetworkingAvailability_Current)
        {
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
        if (coplay_use_lobbies.GetBool())
        {
            if (m_role == eConnectionRole_HOST && m_lobby.IsLobby() && m_lobby.IsValid())
            {
                ConVarRef hostname("hostname");
                std::string map(engine->GetLevelName());
                if (map.length() > std::string("maps/x.bsp").length())//check the map name isnt unreasonably short
                {
                    map.erase(0, 5);// remove "maps/"
                    map.erase(map.length() - 4, 4);// remove ".bsp"
                    SteamMatchmaking()->SetLobbyData(m_lobby, "map", map.c_str());
                }
                SteamMatchmaking()->SetLobbyData(m_lobby, "hostname", hostname.GetString());
                SteamMatchmaking()->SetLobbyMemberLimit(m_lobby, gpGlobals->maxClients);
            }
        }
        else
        {
            if (engine->IsConnected() && coplay_joinfilter.GetInt() != eP2PFilter_CONTROLLED)// Being in a lobby sets the connect string automatically
            {
                INetChannelInfo* netinfo = engine->GetNetChannelInfo();

                std::string IP = netinfo->GetAddress();
                if ((netinfo->IsLoopback() || IP.find("127") == 0) && m_hP2PSocket)
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
                else if (netinfo)
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
        }
        lastSteamRPCUpdate = gpGlobals->realtime;
    }
#endif

    if (!coplay_use_lobbies.GetBool()) //waiting for password on pending clients
    {
        for (int i = 0; i < m_pendingConnections.Count(); i++)
        {
            //Timeout
            extern ConVar coplay_timeoutduration;
            if (m_pendingConnections[i].timeCreated + coplay_timeoutduration.GetFloat() < gpGlobals->realtime)
            {
                SteamNetworkingSockets()->CloseConnection(m_pendingConnections[i].hSteamConnection, k_ESteamNetConnectionEnd_App_ClosedByPeer, "", false);
                m_pendingConnections.Remove(i--);//decrement i because removing one from the vector shifts it all
            }

            //check for responses
            SteamNetworkingMessage_t* InboundSteamMessage;
            if (SteamNetworkingSockets()->ReceiveMessagesOnConnection(m_pendingConnections[i].hSteamConnection, &InboundSteamMessage, 1))
            {
                std::string recvPassword((const char*)(InboundSteamMessage->GetData()), InboundSteamMessage->GetSize());
                Msg("Got password %s\n", recvPassword.c_str());
                if (m_password == recvPassword)
                {
                    CreateSteamConnectionTuple(m_pendingConnections[i].hSteamConnection);
                    SteamNetworkingSockets()->SendMessageToConnection(m_pendingConnections[i].hSteamConnection,
                        COPLAY_NETMSG_OK, sizeof(COPLAY_NETMSG_OK), k_nSteamNetworkingSend_ReliableNoNagle, NULL);
                    m_pendingConnections.Remove(i--);
                }
                else
                {
                    SteamNetworkingSockets()->CloseConnection(m_pendingConnections[i].hSteamConnection, k_ESteamNetConnectionEnd_App_BadPassword, "", false);
                    m_pendingConnections.Remove(i--);
                }
            }
        }
    }
}

void CCoplaySystem::LevelInitPostEntity()//Open p2p automatically
{
    if (GetRole() != eConnectionRole_NOT_CONNECTED || coplay_joinfilter.GetInt() == eP2PFilter_OFF)
        return;

    INetChannelInfo *netinfo = engine->GetNetChannelInfo();
    std::string ip = netinfo->GetAddress();
    if (!(netinfo->IsLoopback() || ip.find("127") == 0))// check its our game and not a dedicated
        return;

    OpenP2PSocket();
}

void CCoplaySystem::LevelShutdownPreEntity()
{
    if (!engine->IsConnected())//server disconnect/shutdown
    {
        CloseAllConnections();
        SetRole(eConnectionRole_NOT_CONNECTED);
    }
}

void CCoplaySystem::RechoosePassword()
{
    static const std::string validchars= "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    m_password.clear();
    for (int i = 0; i < 32; i++)
        m_password += validchars[rand() % validchars.length()];
}

void CCoplaySystem::OpenP2PSocket()
{
    if (!engine->IsConnected())
    {
        ConColorMsg(COPLAY_MSG_COLOR, "You're not currently in a local game.");
        return;
    }
    INetChannelInfo* netinfo = engine->GetNetChannelInfo();
    std::string ip = netinfo->GetAddress();
    if (!(netinfo->IsLoopback() || ip.find("127") == 0))
    {
        ConColorMsg(COPLAY_MSG_COLOR, "You're not currently in a local game.%s\n", netinfo->GetAddress());
        return;
    }
    CloseP2PSocket();
    CloseAllConnections();
    SetRole(eConnectionRole_HOST);
    m_hP2PSocket = SteamNetworkingSockets()->CreateListenSocketP2P(0, 0, NULL);

    if (coplay_use_lobbies.GetBool())
    {
        SteamMatchmaking()->LeaveLobby(m_lobby);
        int filter = coplay_joinfilter.GetInt();
        ELobbyType lobbytype = (ELobbyType)(filter > -1 ? filter : 0);
        SteamMatchmaking()->CreateLobby(lobbytype, gpGlobals->maxClients);
    }
    else
    {
        RechoosePassword();
    }
}

void CCoplaySystem::CloseP2PSocket()
{
    if (GetRole() != eConnectionRole_HOST)
        return;
    CloseAllConnections();
    SteamNetworkingSockets()->CloseListenSocket(m_hP2PSocket);
    m_hP2PSocket = 0;
    if (coplay_use_lobbies.GetBool())
    {
        SteamMatchmaking()->LeaveLobby(m_lobby);
        m_lobby.SetFromUint64(0);
    }
    SetRole(eConnectionRole_CLIENT);
}

void CCoplaySystem::CloseAllConnections(bool waitforjoin)
{
    for (int i = 0; i< m_connections.Count(); i++)
        m_connections[i]->QueueForDeletion();
    if (waitforjoin)
        for (int i = 0; i< m_connections.Count(); i++)
            m_connections[i]->Join();
}

int CCoplaySystem::GetConnectCommand(std::string &out)
{
    out = "";
    if ( GetRole() == eConnectionRole_NOT_CONNECTED )
    {
        return 1;
    }

    uint64 id;
    if (coplay_use_lobbies.GetBool())
    {
        id = GetLobby().ConvertToUint64();
    }
    else
    {
        SteamNetworkingIdentity netID;
        SteamNetworkingSockets()->GetIdentity(&netID);
        id = netID.GetSteamID64();
    }

    if (coplay_joinfilter.GetInt() == eP2PFilter_CONTROLLED)
    {
        if (coplay_use_lobbies.GetBool())
            return 2;
        else
           out = "coplay_connect " + std::to_string(id) + " " + GetPassword();
    }
    else
    {
        out = "coplay_connect " + std::to_string(id);
    }

    return 0;
}

bool CCoplaySystem::CreateSteamConnectionTuple(HSteamNetConnection hConn)
{
    SteamNetConnectionInfo_t newinfo;
    if (!SteamNetworkingSockets()->GetConnectionInfo(hConn, &newinfo))
        return false;

    for (int i = 0; i < m_connections.Count(); i++)
    {
        SteamNetConnectionInfo_t info;
        if (SteamNetworkingSockets()->GetConnectionInfo(m_connections[i]->m_hSteamConnection, &info))
        {
            if (info.m_identityRemote.GetSteamID64() == newinfo.m_identityRemote.GetSteamID64())
            {
                m_connections[i]->QueueForDeletion();
                break;
            }
        }
    }

    CCoplayConnection *connection = new CCoplayConnection(hConn, m_role);

    connection->Start();
    m_connections.AddToTail(connection);

    return true;
}

void CCoplaySystem::SetRole(ConnectionRole newrole)
{
    ConVarRef sv_lan("sv_lan");
    ConVarRef engine_no_focus_sleep("engine_no_focus_sleep");
    switch (newrole)
    {
    case eConnectionRole_HOST:
        sv_lan.SetValue("1");//sv_lan off will heartbeat the server and allow clients to see our ip
        engine_no_focus_sleep.SetValue("0"); // without this, if the host unfocuses from the game everyone lags
        break;
    case eConnectionRole_CLIENT:
        engine_no_focus_sleep.SetValue(engine_no_focus_sleep.GetDefault());
        break;
    case eConnectionRole_NOT_CONNECTED:
        CloseAllConnections();
        if (coplay_use_lobbies.GetBool())
        {
            SteamMatchmaking()->LeaveLobby(m_lobby);
            m_lobby.SetFromUint64(0);
        }
    }

    m_role = newrole;
}

void CCoplaySystem::ConnectionStatusUpdated(SteamNetConnectionStatusChangedCallback_t* pParam)
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
            if (coplay_use_lobbies.GetBool())
            {
                if (IsUserInLobby(m_lobby, pParam->m_info.m_identityRemote.GetSteamID()))
                    SteamNetworkingSockets()->AcceptConnection(pParam->m_hConn);
            }
            else
            {
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
                // Connections in PendingConnections are run in CCoplaySystem::Update() waiting to recieve it
                {
                    SteamNetworkingSockets()->AcceptConnection(pParam->m_hConn);
                    PendingConnection newPending;
                    newPending.hSteamConnection = pParam->m_hConn;
                    newPending.timeCreated = gpGlobals->realtime;
                    m_pendingConnections.AddToTail(newPending);
                }
                break;


                default:
                    SteamNetworkingSockets()->CloseConnection(pParam->m_hConn, k_ESteamNetConnectionEnd_App_NotOpen, "", true);
                break;
                }
            }
        }
        else
        {
           SteamNetworkingSockets()->AcceptConnection(pParam->m_hConn);
        }
        break;

    case k_ESteamNetworkingConnectionState_Connected:
        if (coplay_use_lobbies.GetBool() && GetRole() == eConnectionRole_HOST)
        {
            for (int i = 0; i < m_pendingConnections.Count(); i++)
            {
                if (m_pendingConnections[i].hSteamConnection == pParam->m_hConn)
                {
                    //send a message that we're expecting a password
                    SteamNetworkingSockets()->SendMessageToConnection(pParam->m_hConn, COPLAY_NETMSG_NEEDPASS, sizeof(COPLAY_NETMSG_NEEDPASS),
                        k_nSteamNetworkingSend_Reliable, NULL);
                    return;
                }
            }

            SteamNetworkingSockets()->SendMessageToConnection(pParam->m_hConn, COPLAY_NETMSG_OK, sizeof(COPLAY_NETMSG_OK),
                k_nSteamNetworkingSend_Reliable, NULL);
        }

        if (!CreateSteamConnectionTuple(pParam->m_hConn))
        {
            SteamNetworkingSockets()->CloseConnection(pParam->m_hConn, k_ESteamNetConnectionEnd_App_RemoteIssue, "", true);
            if (coplay_use_lobbies.GetBool() && GetRole() != eConnectionRole_HOST)
                    SteamMatchmaking()->LeaveLobby(m_lobby);
        }

        break;

    case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
        SteamNetworkingSockets()->CloseConnection(pParam->m_hConn, k_ESteamNetConnectionEnd_Misc_Timeout, "", true);
    // TODO - Fix this jesus christ
    // Intentionally no break
    case k_ESteamNetworkingConnectionState_ClosedByPeer:

        if (coplay_use_lobbies.GetBool())
        {
            if (GetRole() != eConnectionRole_HOST)
                SteamMatchmaking()->LeaveLobby(m_lobby);
        }
        else
        {
            for (int i = 0; i < m_pendingConnections.Count(); i++)
            {
                if (m_pendingConnections[i].hSteamConnection == pParam->m_hConn)
                {
                    m_pendingConnections.Remove(i);
                    return;
                }
            }
        }

        for (int i = 0; i < m_connections.Count(); i++)
        {
            if (m_connections[i]->m_hSteamConnection == pParam->m_hConn)
                m_connections[i]->QueueForDeletion();
        }

        break;
    }
}

void CCoplaySystem::LobbyCreated(LobbyCreated_t *pParam)
{
    SteamMatchmaking()->LeaveLobby(m_lobby);//Leave the old lobby if we were in one
    m_lobby = pParam->m_ulSteamIDLobby;
}

void CCoplaySystem::LobbyJoined(LobbyEnter_t *pParam)
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
    SteamMatchmaking()->LeaveLobby(m_lobby);//Leave the old lobby if we were in one
    m_lobby = pParam->m_ulSteamIDLobby;

    std::string cmd = "coplay_connect " + std::to_string(SteamMatchmaking()->GetLobbyOwner(m_lobby).ConvertToUint64());
    engine->ClientCmd_Unrestricted(cmd.c_str());//Just use the same code

}

void CCoplaySystem::LobbyJoinRequested(GameLobbyJoinRequested_t *pParam)
{
    SteamMatchmaking()->JoinLobby(pParam->m_steamIDLobby);
}

void CCoplaySystem::OnLobbyListcmd(LobbyMatchList_t *pLobbyMatchList, bool bIOFailure)
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

void CCoplaySystem::JoinGame(GameRichPresenceJoinRequested_t *pParam)
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
    ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "[Coplay Warning] CCoplaySystem::JoinGame() Was given a bad/invalid/empty command ( %s )!"
                                        "Make sure both parties are updated and that you trust the host you're trying to connect to.\n", cmd.c_str());
}

// ================================================================================================
// 
// Coplay commands
//
// ================================================================================================

void CCoplaySystem::CoplayConnect(const CCommand& args)
{
    if (args.ArgC() < 1)
        return;

    std::string Id = args.Arg(1);
    // Might need to send password later
    if(!coplay_use_lobbies.GetBool())
        m_password = std::string(args.Arg(2));

    CloseAllConnections();

    if (Id.find_first_of('.', 0) != std::string::npos || // normal server, probably
        Id.compare("localhost") == 0) // our own server
    {
		// call the old connect command
        if(m_oldConnectCallback)
            m_oldConnectCallback(args);
        else
        {
			// if we're not overriding for some reason, just call the normal connect command
            std::string cmd = "connect " + Id;
            engine->ClientCmd_Unrestricted(cmd.c_str());
        }
    }
    else // what you're here for
    {
        SteamRelayNetworkStatus_t deets;
        if (SteamNetworkingUtils()->GetRelayNetworkStatus(&deets) != k_ESteamNetworkingAvailability_Current)
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
        if (coplay_use_lobbies.GetBool() && steamid.IsLobby())
        {
            ConColorMsg(COPLAY_MSG_COLOR, "[Coplay] Attempting to join lobby with ID %s....\n", Id.c_str());
            SteamMatchmaking()->JoinLobby(steamid);
            return;
        }

        if (!steamid.BIndividualAccount())
        {
            Warning("Coplay_Connect was called with an invalid SteamID! ( %llu )\n", steamid.ConvertToUint64());
            return;
        }

        if (coplay_use_lobbies.GetBool() && GetLobby().ConvertToUint64() == 0)
        {
            Warning("Coplay_Connect was called with a user SteamID before we were in a lobby!\n");
            return;
        }
        
        SteamNetworkingIdentity netID;
        netID.SetSteamID64(strtoull(Id.c_str(), NULL, 10));
        
        ConColorMsg(COPLAY_MSG_COLOR, "[Coplay] Attempting Connection to user with ID %llu....\n", netID.GetSteamID64());
        SetRole(eConnectionRole_CLIENT);
        SteamNetworkingSockets()->ConnectP2P(netID, 0, 0, NULL);
    }
}

void CCoplaySystem::OpenSocket(const CCommand& args)
{
    OpenP2PSocket();
}

void CCoplaySystem::CloseSocket(const CCommand& args)
{
    CloseP2PSocket();
}

void CCoplaySystem::ListLobbies(const CCommand& args)
{
    SteamAPICall_t apiCall = SteamMatchmaking()->RequestLobbyList();
    m_lobbyListResult.Set(apiCall, this, &CCoplaySystem::OnLobbyListcmd);
}

void CCoplaySystem::PrintAbout(const CCommand& args)
{
    ConColorMsg(COPLAY_MSG_COLOR, "Coplay allows P2P connections in sourcemods. Visit the Github page for more information and source code\n");
    ConColorMsg(COPLAY_MSG_COLOR, "https://github.com/CoaXioN-Games/coplay\n\n");
    ConColorMsg(COPLAY_MSG_COLOR, "The loaded Coplay version is %s.\n\n", COPLAY_VERSION);

    ConColorMsg(COPLAY_MSG_COLOR, "Active Coplay build options:\n");
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

void CCoplaySystem::GetConnectCommand(const CCommand& args)
{
    std::string cmd;
    switch (GetConnectCommand(cmd))
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

void CCoplaySystem::ReRandomizePassword(const CCommand& args)
{
    if (GetRole() != eConnectionRole_HOST)
    {
        ConColorMsg(COPLAY_MSG_COLOR, "You're not currently hosting a game.");
        return;
    }
    RechoosePassword();
}

void CCoplaySystem::ConnectToLobby(const CCommand& args)
{
    // Steam appends '+connect_lobby (id64)' to launch options when boot joining
    std::string cmd = "coplay_connect ";
    cmd += args.ArgS();
    engine->ClientCmd_Unrestricted(cmd.c_str());
}

void CCoplaySystem::InviteToLobby(const CCommand& args)
{
    if (GetLobby().ConvertToUint64() == 0)
    {
        ConColorMsg(COPLAY_MSG_COLOR, "You aren't in a lobby.\n");
        return;
    }
    SteamFriends()->ActivateGameOverlayInviteDialog(GetLobby());
}

// Debug commands
void CCoplaySystem::DebugPrintState(const CCommand& args)
{
    ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "Role: %i\nActive Connections: %i\n", GetRole(), m_connections.Count());
}

void CCoplaySystem::DebugCreateDummyConnection(const CCommand& args)
{
    CCoplayConnection* connection = new CCoplayConnection(NULL, GetRole());

    connection->Start();
    m_connections.AddToTail(connection);
}

void CCoplaySystem::ListInterfaces(const CCommand& args)
{
    IPaddress addr[16];
    int num = SDLNet_GetLocalAddresses(addr, sizeof(addr) / sizeof(IPaddress));
    for (int i = 0; i < num; i++)
        Msg("%i.%i.%i.%i\n", ((uint8*)&addr[i].host)[0], ((uint8*)&addr[i].host)[1], ((uint8*)&addr[i].host)[2], ((uint8*)&addr[i].host)[3]);
}

void CCoplaySystem::DebugSendDummySteam(const CCommand& args)
{
	FOR_EACH_VEC(m_connections, i)
	{
		CCoplayConnection* con = m_connections[i];
		if (!con)
			continue;
		char string[] = "Completely Random Test String (tm)";
		int64 msgout;
		SteamNetworkingSockets()->SendMessageToConnection(con->m_hSteamConnection, string, sizeof(string),
			k_nSteamNetworkingSend_UnreliableNoDelay | k_nSteamNetworkingSend_UseCurrentThread,
			&msgout);
	}
}