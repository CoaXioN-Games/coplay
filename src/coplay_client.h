#ifndef COPLAY_CLIENT_H
#define COPLAY_CLIENT_H
#pragma once

#include "steam/isteamnetworkingsockets.h"
#include "steam/isteamnetworkingutils.h"

class CCoplayConnection;
class CCoplayClient
{
public:
	CCoplayClient();
	~CCoplayClient();

	void ConnectToHost(CSteamID host);
	void CloseConnection();
	bool ConnectionStatusUpdated(SteamNetConnectionStatusChangedCallback_t* pParam);
	bool IsConnected() const { return m_hConn != k_HSteamNetConnection_Invalid; }

private:
	bool CreateConnection(HSteamNetConnection hConnection);

private:
	HSteamNetConnection m_hConn;
	CSteamID m_hostLobby;
	CCoplayConnection* m_pConnection;
};
#endif