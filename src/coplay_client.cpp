#include "cbase.h"
#include "coplay_client.h"
#include <inetchannel.h>
#include <inetchannelinfo.h>
#include "coplay_connection.h"

CCoplayClient::CCoplayClient()
{
}

CCoplayClient::~CCoplayClient()
{
}

void CCoplayClient::ConnectToHost(CSteamID host)
{
	if (IsConnected())
        CloseConnection();

    SteamNetworkingIdentity netID;
    SteamNetworkingSockets()->GetIdentity(&netID);

    ConColorMsg(COPLAY_MSG_COLOR, "[Coplay] Attempting Connection to user with ID %llu....\n", netID.GetSteamID64());
    SteamNetworkingSockets()->ConnectP2P(netID, 0, 0, NULL);
}

extern ConVar coplay_use_lobbies;
extern ConVar coplay_joinfilter;
bool CCoplayClient::ConnectionStatusUpdated(SteamNetConnectionStatusChangedCallback_t* pParam)
{
    bool stateFailed = false;
    switch (pParam->m_info.m_eState)
    {
    case k_ESteamNetworkingConnectionState_Connecting:
        SteamNetworkingSockets()->AcceptConnection(pParam->m_hConn);
        break;

    case k_ESteamNetworkingConnectionState_Connected:
        if (!CreateConnection(pParam->m_hConn))
        {
            SteamNetworkingSockets()->CloseConnection(pParam->m_hConn, k_ESteamNetConnectionEnd_App_RemoteIssue, "", true);
            if (coplay_use_lobbies.GetBool())
            {
                SteamMatchmaking()->LeaveLobby(m_hostLobby);
                m_hostLobby.Clear();
            }
        }
        break;

    case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
        SteamNetworkingSockets()->CloseConnection(pParam->m_hConn, k_ESteamNetConnectionEnd_Misc_Timeout, "", true);
    case k_ESteamNetworkingConnectionState_ClosedByPeer:
        CloseConnection();
		stateFailed = true;
        break;
    }

	return stateFailed;
}

bool CCoplayClient::CreateConnection(HSteamNetConnection hConnection)
{
    SteamNetConnectionInfo_t newinfo;
    if (!SteamNetworkingSockets()->GetConnectionInfo(hConnection, &newinfo))
        return false;

    CloseConnection();
    ConColorMsg(COPLAY_MSG_COLOR, "[Coplay] Connecting to server...\n");
    m_pConnection = new CCoplayConnection(hConnection);
    m_pConnection->ConnectToHost();
	m_pConnection->Start();
    return true;
}

void CCoplayClient::CloseConnection()
{
    if (m_hostLobby != k_steamIDNil)
    {
        SteamMatchmaking()->LeaveLobby(m_hostLobby);
		m_hostLobby.Clear();
    }

    if (m_pConnection)
    {
        m_pConnection->QueueForDeletion();
        m_pConnection->Join();
        delete m_pConnection;
        m_pConnection = nullptr;
    }
}