#ifndef COPLAY_CONNECTION_H
#define COPLAY_CONNECTION_H
#pragma once

#include "coplay.h"
#include <tier0/threadtools.h>
#include "steam/isteamnetworkingsockets.h"
#include "SDL2/SDL_net.h"

//a single SDL/Steam connection pair, clients will only have 0 or 1 of these, one per remote player on the host
class CCoplayConnection : public CThread
{
public:
    CCoplayConnection(HSteamNetConnection hConn, int role);
    void QueueForDeletion(){ m_deletionQueued = true; }

private:
    int Run();

public:
    // only check for inital messaging for passwords, if needed, a connecting client cant know for sure
    bool      m_gameReady;
    UDPsocket m_localSocket = NULL;
    uint16    m_port = 0;
    IPaddress m_sendbackAddress;

    HSteamNetConnection     m_hSteamConnection = 0;
    float                   m_timeStarted;
	int 				   m_role;

private:
    CInterlockedInt m_deletionQueued;
    // For when the steam connection is still being kept alive but there is no actual activity
    float m_lastPacketTime = 0; 
};
#endif