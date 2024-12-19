/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

//================================================
// CoaXioN Implementation of Steam P2P networking on Source SDK: "CoaXioN Coplay"
// Author : Tholp / Jackson S
//================================================
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

void CCoplayClient::ConnectToHost(CSteamID host, std::string passcode)
{
	if (IsConnected())
        CloseConnection();

    SteamNetworkingIdentity netID;
    if (host.IsLobby())
    {
        m_hostLobby = host;
        netID.SetSteamID(SteamMatchmaking()->GetLobbyOwner(host));
    }
    else
        netID.SetSteamID(host);

    ConColorMsg(COPLAY_MSG_COLOR, "[Coplay] Attempting Connection to user with ID %llu....\n", netID.GetSteamID64());
    m_passcode = passcode;
    SteamNetworkingSockets()->ConnectP2P(netID, 0, 0, NULL);
}

bool CCoplayClient::ConnectionStatusUpdated(SteamNetConnectionStatusChangedCallback_t* pParam)
{
    bool stateFailed = false;
    //Msg("%i\n", pParam->m_info.m_eState);
    switch (pParam->m_info.m_eState)
    {
    case k_ESteamNetworkingConnectionState_Connecting:
        SteamNetworkingSockets()->AcceptConnection(pParam->m_hConn);
        break;

    case k_ESteamNetworkingConnectionState_Connected:
        if (!CreateConnection(pParam->m_hConn))
        {
            SteamNetworkingSockets()->CloseConnection(pParam->m_hConn, k_ESteamNetConnectionEnd_App_RemoteIssue, "", true);
            if (UseCoplayLobbies())
            {
                SteamMatchmaking()->LeaveLobby(m_hostLobby);
                m_hostLobby.Clear();
            }
        }
        break;

    case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
        SteamNetworkingSockets()->CloseConnection(pParam->m_hConn, k_ESteamNetConnectionEnd_Misc_Timeout, "timeout", true);
        CloseConnection();
        stateFailed = true;
        break;

    case k_ESteamNetworkingConnectionState_ClosedByPeer:
        SteamNetworkingSockets()->CloseConnection(pParam->m_hConn, k_ESteamNetConnectionEnd_App_ClosedByPeer, "closedbypeer", true);
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
