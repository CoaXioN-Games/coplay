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

// Make, delete and keep track of connections

#include "cbase.h"
#include <coplay.h>
#include <inetchannel.h>
#include <inetchannelinfo.h>
#include <steam/isteamgameserver.h>

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

ConVar coplay_joinfilter("coplay_joinfilter", "1", FCVAR_ARCHIVE, "Whos allowed to connect to our Server?\n"
                         "-1 : Nobody (Coplay inactive)\n"
                         "0  : Anybody\n"
                         "1  : Steam Friends\n"
                         "2  : Invite Only (not yet added)\n");
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

CCoplayConnectionHandler::CCoplayConnectionHandler()
{
    UpdateSleepTime();
}

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
            SetRole(eConnectionRole_NOT_CONNECTED);
            ConColorMsg(COPLAY_MSG_COLOR, "[Coplay] Steam Relay Connection successful!\n");
            ConColorMsg(COPLAY_MSG_COLOR, "[Coplay] Intialization Complete!\n");
        }
    }

#ifdef CLIENT_DLL
    if (gpGlobals->realtime > lastSteamRPCUpdate + 1.0f)
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
                    lastSteamRPCUpdate = gpGlobals->realtime;
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
                lastSteamRPCUpdate = gpGlobals->realtime;
                return;
            }
            char szConnectCMD[64] = "+coplay_connect ";
            V_strcat(szConnectCMD, szIP, sizeof(szConnectCMD));
            SteamFriends()->SetRichPresence("connect", szConnectCMD);
            lastSteamRPCUpdate = gpGlobals->realtime;
        }
    }
#endif
}

void CCoplayConnectionHandler::OpenP2PSocket()
{
    CloseP2PSocket();
    SetRole(eConnectionRole_HOST);
    HP2PSocket =  SteamNetworkingSockets()->CreateListenSocketP2P(0, 0, NULL);
}

void CCoplayConnectionHandler::CloseP2PSocket()
{
    CloseAllConnections();
    SteamNetworkingSockets()->CloseListenSocket(HP2PSocket);
    HP2PSocket = 0;
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
        g_pCoplayConnectionHandler->SetRole(eConnectionRole_CLIENT);
    }

    if (V_strstr(arg, "."))//normal server, probably
    {
        char cmd[64];
        V_snprintf(cmd, sizeof(cmd), "connect %s", arg );
        engine->ClientCmd(cmd);
    }
    else
    {
        SteamRelayNetworkStatus_t deets;
        if ( SteamNetworkingUtils()->GetRelayNetworkStatus(&deets) != k_ESteamNetworkingAvailability_Current)
        {
            Warning("[Coplay Warning] Can't Connect! Connection to Steam Datagram Relay not yet established.\n");
            return;
        }
        SteamNetworkingIdentity netID;
        netID.SetSteamID64(atoll(arg));
        SteamNetworkingConfigValue_t options[1];
        options[0].SetInt32(k_ESteamNetworkingConfig_EnableDiagnosticsUI, 1);

        ConColorMsg(COPLAY_MSG_COLOR, "[Coplay] Attempting Connection to %llu....\n", netID.GetSteamID64());
        SteamNetworkingSockets()->ConnectP2P(netID, 0, sizeof(options)/sizeof(SteamNetworkingConfigValue_t), options);
    }
}
#endif
