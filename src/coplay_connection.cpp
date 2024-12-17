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
#include "coplay_connection.h"
#include "coplay_system.h"
#include <inetchannel.h>
#include <inetchannelinfo.h>

ConVar coplay_timeoutduration("coplay_timeoutduration", "30", FCVAR_ARCHIVE);
ConVar coplay_portrange_begin("coplay_portrange_begin", "3600", FCVAR_ARCHIVE, "Where to start looking for ports to bind on, a range of atleast 64 is recomended.\n");
ConVar coplay_portrange_end  ("coplay_portrange_end", "3700", FCVAR_ARCHIVE, "Where to stop looking for ports to bind on, a range of atleast 64 is recomended.\n");

ConVar coplay_debuglog_socketspam("coplay_debuglog_socketspam", "0", 0, "Prints the number of packets recieved by either interface if more than 0.\n");
ConVar coplay_debuglog_scream("coplay_debuglog_scream", "0", 0, "Yells if the connection loop is working\n");

ConVar coplay_debuglog_socketcreation("coplay_debuglog_socketcreation", "0", 0, "Prints more information when a socket is opened or closed.\n");
ConVar coplay_connectionthread_hz("coplay_connectionthread_hz", "300", FCVAR_ARCHIVE,
    "Number of times to run a connection per second. Only change this if you know what it means.\n",
    true, 10, false, 0);

CCoplayConnection::CCoplayConnection(HSteamNetConnection hConn) : m_localSocket(nullptr), m_port(0), m_sendbackAddress(), m_hSteamConnection(0), m_timeStarted(0)
{
    m_hSteamConnection = hConn;
    m_lastPacketTime = gpGlobals->realtime;
    m_deletionQueued = false;

    UDPsocket sock = NULL;
    // TODO - Do all ports need to be opened?
    for (uint16 port = coplay_portrange_begin.GetInt(); port < coplay_portrange_end.GetInt(); port++)
    {
        sock = SDLNet_UDP_Open(port);
        if(!sock)
        {
			ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "[Coplay Debug] Couldn't bind to port %u\n", port);
            continue;
		}

        m_localSocket = sock;
        m_port = port;
        break;
    }

    if (!sock)
    {
        Warning("[Coplay Error] What do you need all those ports for anyway? (Couldn't bind to a port on range %d-%d!)\n", 
            coplay_portrange_begin.GetInt(), coplay_portrange_end.GetInt());
    }

    IPaddress addr{};
    addr.host = SDL_Swap32(INADDR_LOOPBACK);

    if (CCoplaySystem::GetInstance()->GetRole() == eConnectionRole_CLIENT)
    {
        ConVarRef clientport("clientport");
        addr.port = SDL_Swap16(clientport.GetInt());
    }
    else
    {
        INetChannelInfo* netinfo = engine->GetNetChannelInfo();
        // netchannel is set to NULL when disconnecting as a host. be safe
        if (!netinfo)
        {
            addr.port = SDL_Swap16(27015);
        }
        else
        {
            std::string ip = netinfo->GetAddress();
            if (ip.find(':') == std::string::npos || ip.length() - 1 == ip.find(':'))
            {
                addr.port = SDL_Swap16(27015);
            }
            else
            {
                std::string portstr = ip.substr(ip.find(':') + 1, std::string::npos);
                int port = std::stoi(portstr);
                addr.port = SDL_Swap16(port);
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

void CCoplayConnection::ConnectToHost()
{
    ConColorMsg(COPLAY_MSG_COLOR, "[Coplay] Connecting to server...\n");
	char cmd[128];
    byte ipnum[4];
	*(uint32*)(ipnum) = m_sendbackAddress.host;

	// print out the IP address and port number
	V_snprintf(cmd, sizeof(cmd), "connect %d.%d.%d.%d:%i coplay", ipnum[0], ipnum[1], ipnum[2], ipnum[3], m_port);
	Msg("%s\n", cmd);
    engine->ClientCmd_Unrestricted(cmd);
}

int CCoplayConnection::Run()
{
    ConVarRef net_maxroutable("net_maxroutable"); // Defaults to min( 1260, MTU ), i think.
    m_timeStarted = gpGlobals->realtime;
    m_lastPacketTime = gpGlobals->realtime;
    UDPpacket **LocalInboundPackets = SDLNet_AllocPacketV(COPLAY_MAX_PACKETS, net_maxroutable.GetInt());

    SteamNetworkingMessage_t *InboundSteamMessages[COPLAY_MAX_PACKETS];
    UDPpacket SteamPacket;

    int numSDLRecv;
    int numSteamRecv;

    int64 messageOut;
    
    // TEMP IF TEMP IF TEMP IF
    // FIX ME FIX ME FIX ME
#if 0
    if (!coplay_use_lobbies.GetBool())
    {
        // see if the server needs a password and wait till we're told we will be let in to start forwarding stuff
        while (!m_gameReady && !m_deletionQueued && m_timeStarted + coplay_timeoutduration.GetFloat() > gpGlobals->curtime)
        {
            if (coplay_debuglog_scream.GetBool())
                Msg("Waiting for Server response..\n");
            ThreadSleep(50);
            numSteamRecv = SteamNetworkingSockets()->ReceiveMessagesOnConnection(m_hSteamConnection, InboundSteamMessages, sizeof(InboundSteamMessages));
            for (int i = 0; i < numSteamRecv; i++)
            {

                std::string recvMsg((const char*)(InboundSteamMessages[i]->GetData()));
                if (recvMsg == std::string(COPLAY_NETMSG_NEEDPASS))
                {
                    //Msg("Sending Password %s...\n",g_pCoplayConnectionHandler->Password);

                    // TODO - find less gross way to access password
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
    }
#endif 

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

        // TODO - cache me?
        // TODO - should this be moved to the end?
        int sleepTime = 1000/coplay_connectionthread_hz.GetInt();
        if (coplay_debuglog_scream.GetBool())
        {
            Msg("Sleep %ims", sleepTime);
        }
        ThreadSleep(sleepTime);//dont work too hard

        //Outbound to SDR
        if (coplay_debuglog_scream.GetBool())
        {
            Msg("OUTBOUND START");
        }
        numSDLRecv = SDLNet_UDP_RecvV(m_localSocket, LocalInboundPackets);

        if( numSDLRecv > 0 && coplay_debuglog_socketspam.GetBool())
        {
            ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "[Coplay Debug] SDL %i\n", numSDLRecv);
        }

        if (numSDLRecv == -1)
        {
            // TODO - warn as we don't crash out, I think
            ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "[Coplay Debug] SDL Error! %s\n", SDLNet_GetError());
        }

        for (int j = 0; j < numSDLRecv; j++)
        {
            SteamNetworkingSockets()->SendMessageToConnection(m_hSteamConnection, (const void*)LocalInboundPackets[j]->data,
                                                              LocalInboundPackets[j]->len,
                                                              k_nSteamNetworkingSend_UnreliableNoDelay | k_nSteamNetworkingSend_UseCurrentThread,
                                                              &messageOut);//use unreliable mode, source already handles it, dont do double duty for no reason
            //if (coplay_debuglog_socketspam.GetBool())
            //    ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "[Coplay Debug] Result %i\n", result);
        }

        if (coplay_debuglog_scream.GetBool())
        {
            Msg("OUTBOUND END\n");
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


        for (int j = 0; j < numSteamRecv; j++)
        {
            SteamPacket.data = (uint8*)InboundSteamMessages[j]->GetData();
            SteamPacket.len  = InboundSteamMessages[j]->GetSize();

            if (!SDLNet_UDP_Send(m_localSocket, 1, &SteamPacket))
            {
                ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "[Coplay Debug] Wasnt sent! %s\n", SDLNet_GetError());
            }
        }

        for (int j = 0; j < numSteamRecv; j++)
        {
            InboundSteamMessages[j]->Release();
        }

        if (m_lastPacketTime + coplay_timeoutduration.GetFloat() < gpGlobals->realtime)
        {
            if (coplay_debuglog_socketcreation.GetBool())
                ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "[Coplay Debug] Socket with port %i timed out.\n", m_port);
            QueueForDeletion();
        }
    }

    //Cleanup
    SDLNet_FreePacketV(LocalInboundPackets);
    SDLNet_UDP_Close(m_localSocket);
    SteamNetworkingSockets()->CloseConnection(m_hSteamConnection, k_ESteamNetConnectionEnd_App_ConnectionFinished, "", false);

    if (coplay_debuglog_socketcreation.GetBool())
    {
        ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "[Coplay Debug] Socket with port %i closed.\n", m_port);
    }

    return 0;
}
