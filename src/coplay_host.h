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

	CSteamID GetLobby() { return m_lobby; }

private:
	bool AddConnection(HSteamNetConnection hConnection);

private:
	HSteamListenSocket	m_hSocket;
	CUtlVector<CCoplayConnection*> m_connections;

	bool				m_usingPassword;
	CSteamID			m_lobby;
	std::string			m_password;
};

#endif // COPLAY_HOST_H
