/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

//================================================
// CoaXioN Source SDK p2p networking: "CoaXioN Coplay"
// Author : Tholp / Jackson S
//================================================

#include "cbase.h"
#include "coplay.h"
#include <inetchannel.h>
#include <inetchannelinfo.h>

ConVar coplay_timeoutduration("coplay_timeoutduration", "15", FCVAR_ARCHIVE);
ConVar coplay_portrange_begin("coplay_portrange_begin", "3600", FCVAR_ARCHIVE, "Where to start looking for ports to bind on, a range of atleast 64 is recomended.\n");
ConVar coplay_portrange_end  ("coplay_portrange_end", "3700", FCVAR_ARCHIVE, "Where to stop looking for ports to bind on, a range of atleast 64 is recomended.\n");
ConVar coplay_forceloopback("coplay_forceloopback", "1", FCVAR_ARCHIVE, "Use the loopback interface for making connections instead of other interfaces. Only change this if you have issues.\n");

ConVar coplay_debuglog_socketspam("coplay_debuglog_socketspam", "0", 0, "Prints the number of packets recieved by either interface if more than 0.\n");
ConVar coplay_debuglog_scream("coplay_debuglog_scream", "0", 0, "Yells if the connection loop is working\n");


int CCoplayConnection::Run()
{   
    ConVarRef net_maxroutable("net_maxroutable");
    TimeStarted = gpGlobals->realtime;
    UDPpacket **LocalInboundPackets = SDLNet_AllocPacketV(COPLAY_MAX_PACKETS, net_maxroutable.GetInt());

    SteamNetworkingMessage_t *InboundSteamMessages[COPLAY_MAX_PACKETS];
    UDPpacket SteamPacket;

    int numSDLRecv;
    int numSteamRecv;

    int64 messageOut;
#ifndef COPLAY_USE_LOBBIES
    // see if the server needs a password and wait till we're told we will be let in to start forwarding stuff
    while(!GameReady && !DeletionQueued && TimeStarted + coplay_timeoutduration.GetFloat() > gpGlobals->curtime)
    {
        if (coplay_debuglog_scream.GetBool())
            Msg("Waiting for Server response..\n");
        ThreadSleep(50);
        numSteamRecv = SteamNetworkingSockets()->ReceiveMessagesOnConnection(SteamConnection, InboundSteamMessages, sizeof(InboundSteamMessages));
        for(int i =0; i < numSteamRecv; i++)
        {

            std::string recvMsg((const char*)(InboundSteamMessages[i]->GetData()));
            if (recvMsg == std::string(COPLAY_NETMSG_NEEDPASS))
            {
                //Msg("Sending Password %s...\n",g_pCoplayConnectionHandler->Password);
                SteamNetworkingSockets()->SendMessageToConnection(SteamConnection,
                                                                  g_pCoplayConnectionHandler->Password.c_str(), g_pCoplayConnectionHandler->Password.length(),
                                                                  k_nSteamNetworkingSend_ReliableNoNagle | k_nSteamNetworkingSend_UseCurrentThread, &messageOut);
            }
            else if (recvMsg == std::string(COPLAY_NETMSG_OK))
                GameReady = true;//Server said our password was good, start relaying packets
            else
                ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "[Coplay] Got unexpected handshake message, \"%s\"\n", recvMsg.c_str());
        }
    }

#endif
    if (!DeletionQueued && g_pCoplayConnectionHandler->GetRole() == eConnectionRole_CLIENT)
    {
        ConColorMsg(COPLAY_MSG_COLOR, "[Coplay] Connecting to server...\n");
        std::string cmd;
        uint32 ipnum = SendbackAddress.host;
        cmd = "connect " + std::to_string(((uint8*)&ipnum)[0]) + '.' + std::to_string(((uint8*)&ipnum)[1]) + '.' +
                std::to_string(((uint8*)&ipnum)[2]) + '.' + std::to_string(((uint8*)&ipnum)[3]) + ':' + std::to_string(Port);
        engine->ClientCmd_Unrestricted(cmd.c_str());
    }

    while(!DeletionQueued)
    {
        if (coplay_debuglog_scream.GetBool())
        {
            Msg("LOOP START ");
        }
        if (LocalSocket == NULL ||  SteamConnection == 0)
        {
            Warning("[Coplay Warning] A registered Coplay socket was invalid! Deleting.\n");
            DeletionQueued = true;
            continue;
        }

        if (coplay_debuglog_scream.GetBool())
        {
            Msg("Sleep %ims", g_pCoplayConnectionHandler->msSleepTime);
        }
        ThreadSleep(g_pCoplayConnectionHandler->msSleepTime);//dont work too hard


        //Outbound to SDR
        if (coplay_debuglog_scream.GetBool())
        {
            Msg("OUTBOUND START");
        }
        numSDLRecv = SDLNet_UDP_RecvV(LocalSocket, LocalInboundPackets);
        //numSDLRecv = SDLNet_UDP_Recv(LocalSocket, LocalInboundPackets[0]);
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
            SteamNetworkingSockets()->SendMessageToConnection(SteamConnection, (const void*)LocalInboundPackets[j]->data,
                                                                            LocalInboundPackets[j]->len,
                                                                            k_nSteamNetworkingSend_UnreliableNoDelay | k_nSteamNetworkingSend_UseCurrentThread,
                                                                            &messageOut);//use unreliable mode, source already handles it, dont do double duty for no reason
            //if (coplay_debuglog_socketspam.GetBool())
            //    ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "[Coplay Debug] Result %i\n", result);
        }

        //Inbound from SDR

        numSteamRecv = SteamNetworkingSockets()->ReceiveMessagesOnConnection(SteamConnection, InboundSteamMessages, COPLAY_MAX_PACKETS);

        if (numSteamRecv > 0 && coplay_debuglog_socketspam.GetBool())
        {
            ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "[Coplay Debug] Steam %i\n", numSteamRecv);
        }

        if (numSteamRecv > 0 || engine->IsConnected())
        {
            LastPacketTime = gpGlobals->realtime;
        }


        for (uint8 j = 0; j < numSteamRecv; j++)
        {
            SteamPacket.data = (uint8*)InboundSteamMessages[j]->GetData();
            SteamPacket.len  = InboundSteamMessages[j]->GetSize();

            if (!SDLNet_UDP_Send(LocalSocket, 1, &SteamPacket))
            {
                ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "[Coplay Debug] Wasnt sent! %s\n", SDLNet_GetError());
            }
        }

        for (uint8 j = 0; j < numSteamRecv; j++)
        {
            InboundSteamMessages[j]->Release();
        }

        if (LastPacketTime + coplay_timeoutduration.GetFloat() < gpGlobals->realtime)
        {
            QueueForDeletion();
        }
    }
    //Cleanup

    SDLNet_FreePacketV(LocalInboundPackets);
    SDLNet_UDP_Close(LocalSocket);
    SteamNetworkingSockets()->CloseConnection(SteamConnection, k_ESteamNetConnectionEnd_App_ClosedByPeer, "", false);
    g_pCoplayConnectionHandler->Connections.FindAndRemove(this);

    if (coplay_debuglog_socketcreation.GetBool())
    {
        ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "[Coplay Debug] Socket closed with port %i.\n", Port);
    }
    return 0;
}

CON_COMMAND_F(coplay_debug_senddummy_steam, "", FCVAR_CLIENTDLL)
{
    for (int i = 0; i < g_pCoplayConnectionHandler->Connections.Count(); i++)
    {
        CCoplayConnection* con = g_pCoplayConnectionHandler->Connections[i];
        if (!con)
            continue;
        char string[] = "Completely Random Test String (tm)";
        int64 msgout;
        SteamNetworkingSockets()->SendMessageToConnection(con->SteamConnection, string, sizeof(string),
                                                          k_nSteamNetworkingSend_UnreliableNoDelay | k_nSteamNetworkingSend_UseCurrentThread,
                                                          &msgout);

    }
}

CCoplayConnection::CCoplayConnection(HSteamNetConnection hConn)
{
    if (g_pCoplayConnectionHandler->GetRole() == eConnectionRole_HOST)
        GameReady = true;// Server side always knows if this is ready to go
    else
        GameReady = false;
    SteamConnection = hConn;
    LastPacketTime = gpGlobals->realtime;

    UDPsocket sock = NULL;
    for (uint16 port = coplay_portrange_begin.GetInt(); port < coplay_portrange_end.GetInt(); port++)
    {
        sock = SDLNet_UDP_Open(port);
        if (sock)
        {
            LocalSocket = sock;
            Port = port;
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
        int numlocal = SDLNet_GetLocalAddresses(localaddresses, sizeof(localaddresses)/sizeof(IPaddress));

        // this is kind of a mess, from testing using 192.* addresses only work for some people that have only those...
        for (int i = 0; i < numlocal; i++)
        {
            if (localaddresses[i].host == 0)
                continue;
            uint8 firstoctet = ((uint8*)&localaddresses[i].host)[0];
            if (firstoctet == 127 || firstoctet == 172|| firstoctet == 192)
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
        INetChannelInfo *netinfo = engine->GetNetChannelInfo();
        // netchannel is set to NULL when disconnecting as a host. be safe
        if( !netinfo )
        {
            addr.port = SwapEndian16(27015);
        }
        else
        {
            std::string ip = netinfo->GetAddress();
            if ( ip.find(':') == std::string::npos || ip.length() - 1 == ip.find(':'))
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
    SDLNet_UDP_Bind(LocalSocket, 1, &addr);// "Inbound" Channel
    SendbackAddress = addr;

    if (coplay_debuglog_socketcreation.GetBool())
    {
        ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "[Coplay Debug] New socket : %u\n", Port);
        //ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "[Coplay Debug] New socket : %i:%i\n", SDLNet_UDP_GetPeerAddress(tuple->LocalSocket, 0)->host, SDLNet_UDP_GetPeerAddress(tuple->LocalSocket, 0)->port);
    }
    std::string threadname = "coplayconnection_" + std::to_string(Port);
    SetName(threadname.c_str());
}

CON_COMMAND_F(coplay_listinterfaces, "", FCVAR_CLIENTDLL)
{
    IPaddress addr[16];
    int num = SDLNet_GetLocalAddresses(addr, sizeof(addr) / sizeof(IPaddress));
    for (int i = 0; i < num; i++)
        Msg("%i.%i.%i.%i\n", ((uint8*)&addr[i].host)[0], ((uint8*)&addr[i].host)[1], ((uint8*)&addr[i].host)[2], ((uint8*)&addr[i].host)[3] );
}

