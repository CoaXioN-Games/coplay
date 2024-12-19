/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

//================================================
// CoaXioN Implementation of Steam P2P networking on Source SDK: "CoaXioN Coplay"
// Author : Tholp / Jackson S
//================================================

#ifndef COPLAY_HOST_H
#define COPLAY_HOST_H
#pragma once

#include "steam/isteamnetworkingsockets.h"
#include "steam/isteamnetworkingutils.h"
#include "steam/isteammatchmaking.h"

class CCoplayConnection; 
class CCoplayPendingConnection;

class CCoplayHost
{
public:
	CCoplayHost();
	~CCoplayHost();

	void StartHosting();
	void StopHosting();
	void Update();

	bool ConnectionStatusUpdated(SteamNetConnectionStatusChangedCallback_t* pParam);

	bool IsHosting() const { return m_hSocket != k_HSteamListenSocket_Invalid; }

	void RandomizePasscode();
	std::string GetPasscode(){ return m_passcode; }

	CSteamID GetLobby() { return m_lobby; }
	int GetConnectionCount(){return m_connections.Count();}

private:
	bool AddConnection(HSteamNetConnection hConnection);
	void CreatePendingConnection(HSteamNetConnection hConnection);
	void RemoveConnection(HSteamNetConnection hConnection, int reason, const char *pszDebug, bool bEnableLinger);

private:
#ifdef COPLAY_USE_LOBBIES
	STEAM_CALLBACK(CCoplayHost, LobbyCreated,            LobbyCreated_t);
#else
	void LobbyCreated(LobbyCreated_t *pParam);
#endif
private:
	HSteamListenSocket	m_hSocket;
	CUtlVector<CCoplayConnection*> m_connections;
	CUtlVector<CCoplayPendingConnection> m_pendingConnections;

	bool				m_usingPassword;
	CSteamID			m_lobby;
	std::string			m_passcode;
};

struct CCoplayPendingConnection
{
	CCoplayPendingConnection(HSteamNetConnection connection);
	HSteamNetConnection m_hConnection;
	float m_startTime;
};

#endif // COPLAY_HOST_H
