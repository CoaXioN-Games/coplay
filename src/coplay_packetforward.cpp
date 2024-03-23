/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

//================================================
// CoaXioN Source SDK p2p networking: "CoaXioN Coplay"
// Author : Tholp / Jackson S
// File Last Modified : Mar 21 2024
//================================================

#include "cbase.h"
#include "coplay.h"

ConVar coplay_timeoutduration("coplay_timeoutduration", "45");

ConVar coplay_debuglog_socketspam("coplay_debuglog_socketspam", "0");

int CCoplayConnection::Run()
{
    UDPpacket **LocalInboundPackets = SDLNet_AllocPacketV(COPLAY_MAX_PACKETS + 1, 1500);//normal ethernet MTU size
    LocalInboundPackets[COPLAY_MAX_PACKETS - 1 ] = NULL;

    while(!DeletionQueued)
    {
        if (LocalSocket == NULL ||  SteamConnection == 0)
        {
            Warning("[Coplay Warning] A registered Coplay socket was invalid! Deleting.\n");
            DeletionQueued = true;
            continue;
        }

        //Outbound to SDR
        int numSDLRecv = SDLNet_UDP_RecvV(LocalSocket, LocalInboundPackets);
        if (numSDLRecv > 0)
        {
            if(coplay_debuglog_socketspam.GetBool())
                ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "[Coplay Debug] SDL %i\n", numSDLRecv);
            LastPacketTime = gpGlobals->curtime;
        }
        if (numSDLRecv == -1)
            ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "[Coplay Debug] SDL Error! %s\n", SDLNet_GetError());



        int64 messageOut;
        for (int j = 0; j < numSDLRecv; j++)
        {
            // OutboundSteamMessages[j] = SteamNetworkingUtils()->AllocateMessage(LocalInboundPackets[j]->len);
            // OutboundSteamMessages[j]->m_identityPeer
            //EResult result =
            SteamNetworkingSockets()->SendMessageToConnection(SteamConnection, (const void*)LocalInboundPackets[j]->data,
                                                                            LocalInboundPackets[j]->len,
                                                                            k_nSteamNetworkingSend_UnreliableNoDelay | k_nSteamNetworkingSend_UseCurrentThread,
                                                                            &messageOut);
            //if (coplay_debuglog_socketspam.GetBool())
            //    ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "[Coplay Debug] Result %i\n", result);
        }

        //Inbound from SDR
        SteamNetworkingMessage_t *InboundSteamMessages[COPLAY_MAX_PACKETS];//These cant be reused as easily like SDL's packets


        int numSteamRecv = SteamNetworkingSockets()->ReceiveMessagesOnConnection(SteamConnection, InboundSteamMessages, sizeof(InboundSteamMessages));

        if (numSteamRecv > 0 && coplay_debuglog_socketspam.GetBool())
            ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "[Coplay Debug] Steam %i\n", numSteamRecv);

        for (int j = 0; j < numSteamRecv; j++)
        {
            UDPpacket *packet = SDLNet_AllocPacket(InboundSteamMessages[j]->GetSize() + 16);
            memcpy(packet->data, InboundSteamMessages[j]->GetData(), InboundSteamMessages[j]->GetSize());
            packet->len = InboundSteamMessages[j]->GetSize();

            if (!SDLNet_UDP_Send(LocalSocket, 1, packet))
                ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "[Coplay Debug] Wasnt sent! %s\n", SDLNet_GetError());
        }


        for (int j = 0; j < numSteamRecv; j++)
            InboundSteamMessages[j]->Release();


        if (LastPacketTime + coplay_timeoutduration.GetFloat() < gpGlobals->curtime)
            QueueForDeletion();
    }
    //Cleanup

    SDLNet_FreePacketV(LocalInboundPackets);
    return 0;
}

void CCoplayConnection::OnExit()
{
    SDLNet_UDP_Close(LocalSocket);
    SteamNetworkingSockets()->CloseConnection(SteamConnection, k_ESteamNetConnectionEnd_App_ClosedByPeer, "", false);
    g_pCoplayConnectionHandler->Connections.FindAndRemove(this);

    if (coplay_debuglog_socketcreation.GetBool())
        ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "[Coplay Debug] Socket closed with port %i.\n", Port);
}
