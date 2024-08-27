﻿/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

//================================================
// CoaXioN Source SDK p2p networking: "CoaXioN Coplay"
// Author : Tholp / Jackson S
//================================================

#include "cbase.h"
#include "coplay_connection.h"
#include "coplay_system.h"
#include <inetchannel.h>
#include <inetchannelinfo.h>

ConVar coplay_timeoutduration("coplay_timeoutduration", "15", FCVAR_ARCHIVE);
ConVar coplay_portrange_begin("coplay_portrange_begin", "3600", FCVAR_ARCHIVE, "Where to start looking for ports to bind on, a range of atleast 64 is recomended.\n");
ConVar coplay_portrange_end  ("coplay_portrange_end", "3700", FCVAR_ARCHIVE, "Where to stop looking for ports to bind on, a range of atleast 64 is recomended.\n");
ConVar coplay_forceloopback("coplay_forceloopback", "1", FCVAR_ARCHIVE, "Use the loopback interface for making connections instead of other interfaces. Only change this if you have issues.\n");

ConVar coplay_debuglog_socketspam("coplay_debuglog_socketspam", "0", 0, "Prints the number of packets recieved by either interface if more than 0.\n");
ConVar coplay_debuglog_scream("coplay_debuglog_scream", "0", 0, "Yells if the connection loop is working\n");

CCoplayConnection::CCoplayConnection(HSteamNetConnection hConn) : m_localSocket(nullptr), m_port(0), m_sendbackAddress(), m_hSteamConnection(0), m_timeStarted(0)
{
    m_gameReady = g_pCoplayConnectionHandler->GetRole() == eConnectionRole_HOST; // Server side always knows if this is ready to go
    m_hSteamConnection = hConn;
    m_lastPacketTime = gpGlobals->realtime;

    UDPsocket sock = NULL;
    for (uint16 port = coplay_portrange_begin.GetInt(); port < coplay_portrange_end.GetInt(); port++)
    {
        sock = SDLNet_UDP_Open(port);
        if (sock)
        {
            m_localSocket = sock;
            m_port = port;
            break;
        }
    }

    if (!sock)
    {
        Warning("[Coplay Error] What do you need all those ports for anyway? (Couldn't bind to a port on range 26000-27000!)\n");
        //return false;
    }

    IPaddress addr;
    addr.host = 0;
    //addr.host = SwapEndian32(INADDR_LOOPBACK);//SDLNet wants these in network byte order

    if (coplay_forceloopback.GetBool())
    {
        addr.host = SwapEndian32(INADDR_LOOPBACK);
    }
    else// This else block is only really here because I havent tested the above on enough computers yet
    {
        IPaddress localaddresses[16];
        int numlocal = SDLNet_GetLocalAddresses(localaddresses, sizeof(localaddresses) / sizeof(IPaddress));

        // this is kind of a mess, from testing using 192.* addresses only work for some people that have only those...
        for (int i = 0; i < numlocal; i++)
        {
            if (localaddresses[i].host == 0)
                continue;
            uint8 firstoctet = ((uint8*)&localaddresses[i].host)[0];
            if (firstoctet == 127 || firstoctet == 172 || firstoctet == 192)
                continue;
            addr.host = localaddresses[i].host;
        }

        if (addr.host == 0)
        {
            Warning("[Coplay Warning] Didn't find a suitable local address! Trying the 192.* range..\n");
            for (int i = 0; i < numlocal; i++)
            {
                if (localaddresses[i].host == 0)
                    continue;
                uint8 firstoctet = ((uint8*)&localaddresses[i].host)[0];
                if (firstoctet == 127 || firstoctet == 172)//|| firstoctet == 192
                    continue;
                addr.host = localaddresses[i].host;
            }
        }
        if (addr.host == 0)
        {
            Warning("[Coplay Warning] Still didn't find a suitable local address! Trying loopback..\n");
            addr.host = SwapEndian32(INADDR_LOOPBACK);
        }
    }

    if (g_pCoplayConnectionHandler->GetRole() == eConnectionRole_CLIENT)
    {
        ConVarRef clientport("clientport");
        addr.port = SwapEndian16(clientport.GetInt());
    }
    else
    {
        INetChannelInfo* netinfo = engine->GetNetChannelInfo();
        // netchannel is set to NULL when disconnecting as a host. be safe
        if (!netinfo)
        {
            addr.port = SwapEndian16(27015);
        }
        else
        {
            std::string ip = netinfo->GetAddress();
            if (ip.find(':') == std::string::npos || ip.length() - 1 == ip.find(':'))
            {
                addr.port = SwapEndian16(27015);
            }
            else
            {
                std::string portstr = ip.substr(ip.find(':') + 1, std::string::npos);
                int port = std::stoi(portstr);
                addr.port = SwapEndian16(port);
            }
        }
    }
    SDLNet_UDP_Bind(m_localSocket, 1, &addr);// "Inbound" Channel
    m_sendbackAddress = addr;

    if (coplay_debuglog_socketcreation.GetBool())
    {
        ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "[Coplay Debug] New socket : %u\n", m_port);
    }

    std::string threadname = "coplayconnection_" + std::to_string(m_port);
    SetName(threadname.c_str());
}


int CCoplayConnection::Run()
{   
    ConVarRef net_maxroutable("net_maxroutable");
    m_timeStarted = gpGlobals->realtime;
    UDPpacket **LocalInboundPackets = SDLNet_AllocPacketV(COPLAY_MAX_PACKETS, net_maxroutable.GetInt());

    SteamNetworkingMessage_t *InboundSteamMessages[COPLAY_MAX_PACKETS];
    UDPpacket SteamPacket;

    int numSDLRecv;
    int numSteamRecv;

    int64 messageOut;
#ifndef COPLAY_USE_LOBBIES
    // see if the server needs a password and wait till we're told we will be let in to start forwarding stuff
    while(!m_gameReady && !m_deletionQueued && m_timeStarted + coplay_timeoutduration.GetFloat() > gpGlobals->curtime)
    {
        if (coplay_debuglog_scream.GetBool())
            Msg("Waiting for Server response..\n");
        ThreadSleep(50);
        numSteamRecv = SteamNetworkingSockets()->ReceiveMessagesOnConnection(m_hSteamConnection, InboundSteamMessages, sizeof(InboundSteamMessages));
        for(int i =0; i < numSteamRecv; i++)
        {

            std::string recvMsg((const char*)(InboundSteamMessages[i]->GetData()));
            if (recvMsg == std::string(COPLAY_NETMSG_NEEDPASS))
            {
                //Msg("Sending Password %s...\n",g_pCoplayConnectionHandler->Password);
                SteamNetworkingSockets()->SendMessageToConnection(m_hSteamConnection,
                                                                  g_pCoplayConnectionHandler->m_password.c_str(), g_pCoplayConnectionHandler->m_password.length(),
                                                                  k_nSteamNetworkingSend_ReliableNoNagle | k_nSteamNetworkingSend_UseCurrentThread, &messageOut);
            }
            else if (recvMsg == std::string(COPLAY_NETMSG_OK))
                m_gameReady = true;//Server said our password was good, start relaying packets
            else
                ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "[Coplay] Got unexpected handshake message, \"%s\"\n", recvMsg.c_str());
        }
    }

#endif
    if (!m_deletionQueued && g_pCoplayConnectionHandler->GetRole() == eConnectionRole_CLIENT)
    {
        ConColorMsg(COPLAY_MSG_COLOR, "[Coplay] Connecting to server...\n");
        std::string cmd;
        uint32 ipnum = m_sendbackAddress.host;
        cmd = "connect " + std::to_string(((uint8*)&ipnum)[0]) + '.' + std::to_string(((uint8*)&ipnum)[1]) + '.' +
                std::to_string(((uint8*)&ipnum)[2]) + '.' + std::to_string(((uint8*)&ipnum)[3]) + ':' + std::to_string(m_port);
        engine->ClientCmd_Unrestricted(cmd.c_str());
    }

    while(!m_deletionQueued)
    {
        if (coplay_debuglog_scream.GetBool())
        {
            Msg("LOOP START ");
        }
        if (m_localSocket == NULL || m_hSteamConnection == 0)
        {
            Warning("[Coplay Warning] A registered Coplay socket was invalid! Deleting.\n");
            m_deletionQueued = true;
            continue;
        }

        if (coplay_debuglog_scream.GetBool())
        {
            Msg("Sleep %ims", g_pCoplayConnectionHandler->m_msSleepTime);
        }
        ThreadSleep(g_pCoplayConnectionHandler->m_msSleepTime);//dont work too hard


        //Outbound to SDR
        if (coplay_debuglog_scream.GetBool())
        {
            Msg("OUTBOUND START");
        }
        numSDLRecv = SDLNet_UDP_RecvV(m_localSocket, LocalInboundPackets);
        //numSDLRecv = SDLNet_UDP_Recv(m_localSocket, LocalInboundPackets[0]);
        if (coplay_debuglog_scream.GetBool())
        {
            Msg("OUTBOUND END\n");
        }

        if( numSDLRecv > 0 && coplay_debuglog_socketspam.GetBool())
        {
            ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "[Coplay Debug] SDL %i\n", numSDLRecv);
        }

        if (numSDLRecv == -1)
        {
            ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "[Coplay Debug] SDL Error! %s\n", SDLNet_GetError());
        }


        for (uint8 j = 0; j < numSDLRecv; j++)
        {
            SteamNetworkingSockets()->SendMessageToConnection(m_hSteamConnection, (const void*)LocalInboundPackets[j]->data,
                                                                            LocalInboundPackets[j]->len,
                                                                            k_nSteamNetworkingSend_UnreliableNoDelay | k_nSteamNetworkingSend_UseCurrentThread,
                                                                            &messageOut);//use unreliable mode, source already handles it, dont do double duty for no reason
            //if (coplay_debuglog_socketspam.GetBool())
            //    ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "[Coplay Debug] Result %i\n", result);
        }

        //Inbound from SDR

        numSteamRecv = SteamNetworkingSockets()->ReceiveMessagesOnConnection(m_hSteamConnection, InboundSteamMessages, COPLAY_MAX_PACKETS);

        if (numSteamRecv > 0 && coplay_debuglog_socketspam.GetBool())
        {
            ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "[Coplay Debug] Steam %i\n", numSteamRecv);
        }

        if (numSteamRecv > 0 || engine->IsConnected())
        {
            m_lastPacketTime = gpGlobals->realtime;
        }


        for (uint8 j = 0; j < numSteamRecv; j++)
        {
            SteamPacket.data = (uint8*)InboundSteamMessages[j]->GetData();
            SteamPacket.len  = InboundSteamMessages[j]->GetSize();

            if (!SDLNet_UDP_Send(m_localSocket, 1, &SteamPacket))
            {
                ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "[Coplay Debug] Wasnt sent! %s\n", SDLNet_GetError());
            }
        }

        for (uint8 j = 0; j < numSteamRecv; j++)
        {
            InboundSteamMessages[j]->Release();
        }

        if (m_lastPacketTime + coplay_timeoutduration.GetFloat() < gpGlobals->realtime)
        {
            QueueForDeletion();
        }
    }
    //Cleanup

    SDLNet_FreePacketV(LocalInboundPackets);
    SDLNet_UDP_Close(m_localSocket);
    SteamNetworkingSockets()->CloseConnection(m_hSteamConnection, k_ESteamNetConnectionEnd_App_ClosedByPeer, "", false);
    g_pCoplayConnectionHandler->m_connections.FindAndRemove(this);

    if (coplay_debuglog_socketcreation.GetBool())
    {
        ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "[Coplay Debug] Socket closed with port %i.\n", m_port);
    }
    return 0;
}

CON_COMMAND_F(coplay_debug_senddummy_steam, "", FCVAR_CLIENTDLL)
{
    for (int i = 0; i < g_pCoplayConnectionHandler->m_connections.Count(); i++)
    {
        CCoplayConnection* con = g_pCoplayConnectionHandler->m_connections[i];
        if (!con)
            continue;
        char string[] = "Completely Random Test String (tm)";
        int64 msgout;
        SteamNetworkingSockets()->SendMessageToConnection(con->m_hSteamConnection, string, sizeof(string),
                                                          k_nSteamNetworkingSend_UnreliableNoDelay | k_nSteamNetworkingSend_UseCurrentThread,
                                                          &msgout);
    }
}

CON_COMMAND_F(coplay_listinterfaces, "", FCVAR_CLIENTDLL)
{
    IPaddress addr[16];
    int num = SDLNet_GetLocalAddresses(addr, sizeof(addr) / sizeof(IPaddress));
    for (int i = 0; i < num; i++)
        Msg("%i.%i.%i.%i\n", ((uint8*)&addr[i].host)[0], ((uint8*)&addr[i].host)[1], ((uint8*)&addr[i].host)[2], ((uint8*)&addr[i].host)[3] );
}