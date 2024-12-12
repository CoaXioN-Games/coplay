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

// TODO: the way we use lobbies is pretty much for enumeration only, offer a way to override default behavior

class CCoplayConnection;
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
	void RemoveConnection(HSteamNetConnection hConnection, int reason, const char *pszDebug, bool bEnableLinger);

private:
	STEAM_CALLBACK(CCoplayHost, LobbyCreated,            LobbyCreated_t);

private:
	HSteamListenSocket	m_hSocket;
	CUtlVector<CCoplayConnection*> m_connections;

	bool				m_usingPassword;
	CSteamID			m_lobby;
	std::string			m_passcode;
};

#endif // COPLAY_HOST_H
