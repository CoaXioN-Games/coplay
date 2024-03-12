//================================================
// CoaXioN Source SDK p2p networking: "CoaXioN Coplay"
// Author : Tholp / Jackson S
// File Last Modified : Mar 8 2024
//================================================

//Deal with connections

// Game/Server side GameSystems dont init until a map is loaded
// Consequently we have to have this on both client and server, for the server we kill it if not dedicated since we dont need this twice

#include <coplay.h>
#include <inetchannel.h>
#include <inetchannelinfo.h>
#include <steam/isteamgameserver.h>

ConVar coplay_joinfilter("coplay_joinfilter", "1", 0);
ConVar coplay_debuglog_socketcreation("coplay_debuglog_socketcreation", "0", 0);

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
            Error("Steam API Failed to Init! (%s %i)\n", __FILE__, __LINE__);
        }
        SteamNetworkingUtils()->InitRelayNetworkAccess();

        ConColorMsg(COPLAY_MSG_COLOR, "[Coplay] Initialization started...\n");
        g_pCoplayConnectionHandler = new CCoplayConnectionHandler;
        return true;
    }
}coplaybootstrap;

bool CCoplayConnectionHandler::Init()
{
    //memset(&SteamConnections, 0, sizeof(SteamConnections));


    packethandler = new CCoplayPacketHandler;
    packethandler->Start();
    return true;
}

void CCoplayConnectionHandler::Update(float frametime)
{
    static bool checkavail = true;
    static float lastSteamRPCUpdate = 0.0f;
    SteamAPI_RunCallbacks();
    if (checkavail)
    {
        SteamRelayNetworkStatus_t deets;
        if ( SteamNetworkingUtils()->GetRelayNetworkStatus(&deets) == k_ESteamNetworkingAvailability_Current)
        {
            checkavail = false;
            Role = eConnectionRole_CLIENT;
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

            unsigned int nIP = 0;
            char szIP[32];
            if( pChannel->IsLoopback() && HP2PSocket)
            {
                SteamNetworkingIdentity netID;
                if (SteamNetworkingSockets()->GetIdentity(&netID))
                {
                    V_snprintf(szIP, sizeof(szIP), "%u", netID.GetSteamID64());
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
            char szConnectCMD[64] = "+coplayconnect ";
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
    SteamNetworkingSockets()->CloseListenSocket(HP2PSocket);
    HP2PSocket = 0;
    Role = eConnectionRole_CLIENT;
}
CON_COMMAND(coplay_opensocket, "Open p2p listener")
{
    g_pCoplayConnectionHandler->OpenP2PSocket();
}

void CCoplayConnectionHandler::CreateSteamConnectionTuple(HSteamNetConnection hConn)
{
    CoplaySteamSocketTuple *tuple = new CoplaySteamSocketTuple;
    tuple->SteamConnection = hConn;
    tuple->LocalSocket = SDLNet_UDP_Open(0);
    if (coplay_debuglog_socketcreation.GetBool())
        ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "[Coplay Debug] New socket port: %i", SDLNet_UDP_GetPeerAddress(tuple->LocalSocket, -1)->port);
    SteamConnections.AddToTail(tuple);
}

CON_COMMAND_F(coplay_debug_createdummytuple, "Create a empty tuple", FCVAR_HIDDEN)
{
    g_pCoplayConnectionHandler->CreateSteamConnectionTuple(NULL);
}

void CCoplayConnectionHandler::ConnectionStatusUpdated(SteamNetConnectionStatusChangedCallback_t* pParam)
{
    SteamNetworkingIdentity ID = pParam->m_info.m_identityRemote;
    switch (pParam->m_info.m_eState)
    {
    case k_ESteamNetworkingConnectionState_Connecting:
        //New incoming connection
        if (Role != eConnectionRole_HOST) //Clients only init connections
        {
            SteamNetworkingSockets()->CloseConnection(pParam->m_hConn, k_ESteamNetConnectionEnd_NotOpen, "", false);
            break;
        }
        //TODO: reject if full

        switch (coplay_joinfilter.GetInt())
        {
        case eP2PFilter_NONE:
            SteamNetworkingSockets()->CloseConnection(pParam->m_hConn, k_ESteamNetConnectionEnd_NotOpen, "", false);
            break;
        case eP2PFilter_EVERYONE:
            SteamNetworkingSockets()->AcceptConnection(pParam->m_hConn);
            CreateSteamConnectionTuple(pParam->m_hConn);
            break;
        case eP2PFilter_FRIENDS:
            if (SteamFriends()->HasFriend(ID.GetSteamID(), k_EFriendFlagImmediate))
            {
                SteamNetworkingSockets()->AcceptConnection(pParam->m_hConn);
                CreateSteamConnectionTuple(pParam->m_hConn);
            }
            break;
        case eP2PFilter_INVITEONLY:
            //todo ..

        default:
            SteamNetworkingSockets()->CloseConnection(pParam->m_hConn, k_ESteamNetConnectionEnd_NotOpen, "", false);
        }
        break;
    }
}
#ifdef CLIENT_DLL
void CCoplayConnectionHandler::JoinGame(GameRichPresenceJoinRequested_t *pParam)
{
    char cmd[k_cchMaxRichPresenceValueLength];
    V_strcpy(cmd, pParam->m_rgchConnect);
    V_StrRight(cmd, -1, cmd, sizeof(cmd));
    engine->ClientCmd(cmd);
}

CON_COMMAND(coplay_connect, "connect wrapper that adds coplay functionality")
{
    if (args.ArgC() < 1)
        return;
    char arg[128];
    V_snprintf(arg, sizeof(arg), "%s", args.ArgS());

    if (V_strstr(arg, "."))
    {
        char cmd[64];
        V_snprintf(cmd, sizeof(cmd), "connect %s", arg );
        engine->ClientCmd(cmd);
    }
    else
    {
        SteamNetworkingIdentity netID;
        netID.SetSteamID64(atoll(arg));
        SteamNetworkingSockets()->ConnectP2P(netID, 0, 0, NULL);
    }
}
#endif
