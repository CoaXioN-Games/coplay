#ifndef COPLAY_CONNECTION_H
#define COPLAY_CONNECTION_H
#pragma once
#include <tier0/threadtools.h>
#include "steam/isteamnetworkingsockets.h"
#include "SDL2/SDL_net.h"
#include "coplay.h"

///a single SDL/Steam connection pair, clients will only have 0 or 1 of these, one per remote player on the host
class CCoplayConnection : public CThread
{
    int Run();
public:
    CCoplayConnection(HSteamNetConnection hConn, ConnectionRole role, unsigned sleepTime);
    void QueueForDeletion() { m_deletionQueued = true; }

public:
    HSteamNetConnection     m_hSteamConnection;

private:
    bool      m_gameReady;// only check for inital messaging for passwords, if needed, a connecting client cant know for sure
    UDPsocket m_localSocket;
    uint16    m_port ;
    IPaddress m_sendbackAddress;
    float                   m_timeStarted;
    CInterlockedInt         m_deletionQueued;
    float m_lastPacketTime;//This is for when the steam connection is still being kept alive but there is no actual activity
	ConnectionRole m_role;
    unsigned m_sleepTime;
};

#endif