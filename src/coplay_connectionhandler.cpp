/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

//================================================
// CoaXioN Source SDK p2p networking: "CoaXioN Coplay"
// Author : Tholp / Jackson S
// File Last Modified : Mar 21 2024
//================================================

//Deal with connections

// Game/Server side GameSystems dont init until a map is loaded
// Consequently we have to have this on both client and server, for the server we kill it if not dedicated since we dont need this twice

#include "cbase.h"
#include <coplay.h>
#include <inetchannel.h>
#include <inetchannelinfo.h>
#include <steam/isteamgameserver.h>

ConVar coplay_joinfilter("coplay_joinfilter", "1", 0);

ConVar coplay_debuglog_socketcreation("coplay_debuglog_socketcreation", "0", 0);
ConVar coplay_debuglog_steamconnstatus("coplay_debuglog_steamconnstatus", "0", 0);


CCoplayConnectionHandler *g_pCoplayConnectionHandler;

uint32 SwapEndian32(uint32 num)
{
    byte newnum[4];
    newnum[0] = ((byte*)&num)[3];
    newnum[1] = ((byte*)&num)[2];
    newnum[2] = ((byte*)&num)[1];
    newnum[3] = ((byte*)&num)[0];
    return *((uint32*)newnum);
}

uint16 SwapEndian16(uint16 num)
{
    byte newnum[2];
    newnum[0] = ((byte*)&num)[1];
    newnum[1] = ((byte*)&num)[0];
    return *((uint16*)newnum);
}

class CCoplaySteamBootstrap : public CAutoGameSystem
// we need to insure the steam api is loaded before we make any objects that need callbacks ( CCoplayConnectionHandler )
{
public:
    virtual bool Init()
    {
    #ifdef GAME_DLL
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

void CCoplayConnectionHandler::Update(float frametime)
{
    static bool checkavail = true;
    static float lastSteamRPCUpdate = 0.0f;
    SteamAPI_RunCallbacks();
    if (checkavail) //The callback specifically to check this is registered by the engine already it seems
    {
        SteamRelayNetworkStatus_t deets;
        if ( SteamNetworkingUtils()->GetRelayNetworkStatus(&deets) == k_ESteamNetworkingAvailability_Current)
        {
            checkavail = false;
            Role = eConnectionRole_NOT_CONNECTED;
            ConColorMsg(COPLAY_MSG_COLOR, "[Coplay] Steam Relay Connection successful!\n");
            ConColorMsg(COPLAY_MSG_COLOR, "[Coplay] Intialization Complete!\n");
        }
    }

#ifdef CLIENT_DLL
    if (gpGlobals->curtime > lastSteamRPCUpdate + 1.0f)
    {
        if (engine->IsConnected())
        {
            INetChannel *pChannel = reinterpret_cast< INetChannel * >(engine->GetNetChannelInfo());

            char szIP[32];
            if( pChannel->IsLoopback() && HP2PSocket)
            {
                SteamNetworkingIdentity netID;
                if (SteamNetworkingSockets()->GetIdentity(&netID))
                {
                    V_snprintf(szIP, sizeof(szIP), "%llu", netID.GetSteamID64());
                }
                else
                {
                    lastSteamRPCUpdate = gpGlobals->curtime;
                    return; 
                }
            }
            else if( pChannel )
            {
                V_snprintf(szIP, sizeof(szIP), "%s", pChannel->GetAddress());
            }
            else
            {
                SteamFriends()->SetRichPresence("connect", "");
                lastSteamRPCUpdate = gpGlobals->curtime;
                return;
            }
            char szConnectCMD[64] = "+coplay_connect ";
            V_strcat(szConnectCMD, szIP, sizeof(szConnectCMD));
            SteamFriends()->SetRichPresence("connect", szConnectCMD);
            lastSteamRPCUpdate = gpGlobals->curtime;
        }
    }
#endif
}

void CCoplayConnectionHandler::OpenP2PSocket()
{
    CloseP2PSocket();
    Role = eConnectionRole_HOST;
    HP2PSocket =  SteamNetworkingSockets()->CreateListenSocketP2P(0, 0, NULL);
}

void CCoplayConnectionHandler::CloseP2PSocket()
{
    CloseAllConnections();
    SteamNetworkingSockets()->CloseListenSocket(HP2PSocket);
    HP2PSocket = 0;
    Role = eConnectionRole_CLIENT;
}

void CCoplayConnectionHandler::CloseAllConnections()
{
    for (int i = 0; i< Connections.Count(); i++)
        Connections[i]->QueueForDeletion();
    for (int i = 0; i< Connections.Count(); i++)
        Connections[i]->Join();
}

CON_COMMAND_F(coplay_opensocket, "Open p2p listener", FCVAR_CLIENTDLL)
{
    g_pCoplayConnectionHandler->OpenP2PSocket();
}

bool CCoplayConnectionHandler::CreateSteamConnectionTuple(HSteamNetConnection hConn)
{
    CCoplayConnection *connection = new CCoplayConnection(hConn);
    int timeout = 50;
    while (timeout > 0)// TODO: Should probably change this..
    {
        int port = RandomInt(26000, 65535);
        UDPsocket sock = SDLNet_UDP_Open(port);
        if (sock)
        {
            connection->LocalSocket = sock;
            connection->Port = port;
            break;
        }
        timeout--;
    }

    if (timeout == 0)
    {
        Warning("[Coplay Error] What do you need all those ports for anyway? (Couldn't bind to a port on range 26000-65535 after 50 retries!)\n");
        return false;
    }

    IPaddress addr;
    IPaddress localaddresses[16];
    int numlocal = SDLNet_GetLocalAddresses(localaddresses, sizeof(localaddresses)/sizeof(IPaddress));

    for (int i = 0; i < numlocal; i++)
    {
        if (localaddresses[i].host == 0)
            continue;
        uint8 firstoctet = ((uint8*)&localaddresses[i].host)[0];
        if (firstoctet == 127 || firstoctet == 172)//|| firstoctet == 192
            continue;
        addr.host = localaddresses[i].host;
    }

    if (Role == eConnectionRole_CLIENT)
        addr.port = SwapEndian16(27005);//SDLNet wants these in network byte order
    else
        addr.port = SwapEndian16(27015);// default server port, check for this proper later
    SDLNet_UDP_Bind(connection->LocalSocket, 1, &addr);// "Inbound" Channel

    if (coplay_debuglog_socketcreation.GetBool())
    {
        ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "[Coplay Debug] New socket : %u\n", connection->Port);
        //ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "[Coplay Debug] New socket : %i:%i\n", SDLNet_UDP_GetPeerAddress(tuple->LocalSocket, 0)->host, SDLNet_UDP_GetPeerAddress(tuple->LocalSocket, 0)->port);
    }
    char threadname[32];
    V_snprintf(threadname, sizeof(threadname), "coplayconnection%i",addr.port);
    connection->SetName(threadname);

    connection->Start();
    Connections.AddToTail(connection);

    if (Role == eConnectionRole_CLIENT)
    {
        char cmd[64];
        uint32 ipnum = addr.host;
        V_snprintf(cmd, sizeof(cmd), "connect %i.%i.%i.%i:%i", ((uint8*)&ipnum)[0], ((uint8*)&ipnum)[1], ((uint8*)&ipnum)[2], ((uint8*)&ipnum)[3], connection->Port);
        engine->ClientCmd(cmd);
    }

    return true;
}

// CON_COMMAND_F(coplay_debug_createdummytuple, "Create a empty tuple", FCVAR_HIDDEN)
// {
//     g_pCoplayConnectionHandler->CreateSteamConnectionTuple(NULL);
// }

void CCoplayConnectionHandler::ConnectionStatusUpdated(SteamNetConnectionStatusChangedCallback_t* pParam)
{
    SteamNetworkingIdentity ID = pParam->m_info.m_identityRemote;
    if (coplay_debuglog_steamconnstatus.GetBool())
        ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "[Coplay Debug] Connection status updated: Coplay role: %i, Peer SteamID64: %u,\n Param: %i\n Old Param: %i\n",
                    Role, pParam->m_info.m_identityRemote.GetSteamID64(), pParam->m_info.m_eState, pParam->m_eOldState);

    switch (pParam->m_info.m_eState)
    {
    case k_ESteamNetworkingConnectionState_Connecting:
        switch (coplay_joinfilter.GetInt())
        {
        case eP2PFilter_NONE:
            SteamNetworkingSockets()->CloseConnection(pParam->m_hConn, k_ESteamNetConnectionEnd_App_NotOpen, "", false);
            break;
        case eP2PFilter_EVERYONE:
            SteamNetworkingSockets()->AcceptConnection(pParam->m_hConn);
            break;
        case eP2PFilter_FRIENDS:
            if (SteamFriends()->HasFriend(ID.GetSteamID(), k_EFriendFlagImmediate))
                SteamNetworkingSockets()->AcceptConnection(pParam->m_hConn);
            break;
        case eP2PFilter_INVITEONLY:
            //TODO ..

        default:
            SteamNetworkingSockets()->CloseConnection(pParam->m_hConn, k_ESteamNetConnectionEnd_App_NotOpen, "", false);
        }
        break;
    case k_ESteamNetworkingConnectionState_Connected:
        if (!CreateSteamConnectionTuple(pParam->m_hConn))
            SteamNetworkingSockets()->CloseConnection(pParam->m_hConn, k_ESteamNetConnectionEnd_App_PortsFilled, "", NULL);

        break;

    case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
        SteamNetworkingSockets()->CloseConnection(pParam->m_hConn, k_ESteamNetConnectionEnd_Misc_Timeout, "", NULL);// Close immediately, we dont have a need for it anymore
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

#ifdef CLIENT_DLL
void CCoplayConnectionHandler::JoinGame(GameRichPresenceJoinRequested_t *pParam)
{
    char cmd[k_cchMaxRichPresenceValueLength];
    V_strcpy(cmd, pParam->m_rgchConnect);
    V_StrRight(cmd, -1, cmd, sizeof(cmd));//remove the + at the start
    engine->ClientCmd(cmd);
}

CON_COMMAND(coplay_connect, "connect wrapper that adds coplay functionality")
{
    if (args.ArgC() < 1)
        return;
    char arg[128];
    V_snprintf(arg, sizeof(arg), "%s", args.ArgS());
    if(g_pCoplayConnectionHandler)
    {
        g_pCoplayConnectionHandler->CloseAllConnections();
        g_pCoplayConnectionHandler->Role = eConnectionRole_CLIENT;
    }

    if (V_strstr(arg, "."))//normal server
    {
        char cmd[64];
        V_snprintf(cmd, sizeof(cmd), "connect %s", arg );
        engine->ClientCmd(cmd);
    }
    else
    {
        SteamNetworkingIdentity netID;
        netID.SetSteamID64(atoll(arg));
        SteamNetworkingConfigValue_t options[1];
        options[0].SetInt32(k_ESteamNetworkingConfig_EnableDiagnosticsUI, 1);
        SteamNetworkingSockets()->ConnectP2P(netID, 0, sizeof(options)/sizeof(SteamNetworkingConfigValue_t), options);
    }
}
#endif
