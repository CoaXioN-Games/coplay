//================================================
// CoaXioN Source SDK p2p networking: "CoaXioN Coplay"
// Author : Tholp / Jackson S
// File Last Modified : Mar 8 2024
//================================================

#include "cbase.h"
#include "coplay.h"
#define COPLAY_MAX_PACKETS 16

ConVar coplay_debuglog_socketspam("coplay_debuglog_socketspam", "0");
int CCoplayPacketHandler::Run()
{
    CoplaySteamSocketTuple *sockettuple;
    UDPpacket **LocalInboundPackets = SDLNet_AllocPacketV(COPLAY_MAX_PACKETS, 1500);//normal ethernet MTU size
    LocalInboundPackets[COPLAY_MAX_PACKETS - 1 ] = NULL;


    //ConColorMsg(COPLAY_MSG_COLOR, "[Coplay] Loop Started.\n");

    while(ShouldRun)
    {
        for (int i = 0; i < g_pCoplayConnectionHandler->SteamConnections.Count(); i++)
        {

            sockettuple = g_pCoplayConnectionHandler->SteamConnections[i];
            if (sockettuple->LocalSocket == NULL
            ||  sockettuple->SteamConnection == 0)
            {
                Warning("Listed Coplay socket was invalid!\n");
                sockettuple->DeletionQueued = true;
                continue;
            }

            //Outbound to SDR
            int numSDLRecv = SDLNet_UDP_RecvV(sockettuple->LocalSocket, LocalInboundPackets);
            if (numSDLRecv > 0 && coplay_debuglog_socketspam.GetBool())
                ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "[Coplay Debug] SDL %i\n", numSDLRecv);
            if (numSDLRecv == -1)
                ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "[Coplay Debug] SDL Error! %s\n", SDLNet_GetError());



            int64 messageOut;
            for (int j = 0; j < numSDLRecv; j++)
            {
                // OutboundSteamMessages[j] = SteamNetworkingUtils()->AllocateMessage(LocalInboundPackets[j]->len);
                // OutboundSteamMessages[j]->m_identityPeer
                EResult result = SteamNetworkingSockets()->SendMessageToConnection(sockettuple->SteamConnection, (const void*)LocalInboundPackets[j]->data,
                                                                  LocalInboundPackets[j]->len, k_nSteamNetworkingSend_Unreliable, &messageOut);
                if (coplay_debuglog_socketspam.GetBool())
                    ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "[Coplay Debug] Result %i\n", result);
            }

            //Inbound from SDR
            SteamNetworkingMessage_t *InboundSteamMessages[COPLAY_MAX_PACKETS];
            int numSteamRecv = SteamNetworkingSockets()->ReceiveMessagesOnConnection(sockettuple->SteamConnection, InboundSteamMessages, sizeof(InboundSteamMessages));

            if (numSteamRecv > 0 && coplay_debuglog_socketspam.GetBool())
                ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "[Coplay Debug] Steam %i\n", numSteamRecv);

            for (int j = 0; j < numSteamRecv; j++)
            {
                UDPpacket *packet = SDLNet_AllocPacket(InboundSteamMessages[j]->GetSize() + 16);
                memcpy(packet->data, InboundSteamMessages[j]->GetData(), InboundSteamMessages[j]->GetSize());
                packet->len = InboundSteamMessages[j]->GetSize();
                // UDPpacket packet;
                // packet.data = (uint8*)(InboundSteamMessages[j]->GetData());
                // packet.len  = InboundSteamMessages[j]->GetSize();
                //packet->address = IPaddress{INADDR_LOOPBACK, 27015};
                // if (g_pCoplayConnectionHandler->GetConnectionRole() == eConnectionRole_CLIENT)
                //     packet->address.port = 27005; //normal client port
                // else
                //     packet->address.port = 27015; //default server port. TODO: check for what this is!
                if (!SDLNet_UDP_Send(sockettuple->LocalSocket, 1, packet))
                    ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "[Coplay Debug] Wasnt sent! %s\n", SDLNet_GetError());
            }

            //Cleanup
            for (int j = 0; j < numSteamRecv; j++)
                InboundSteamMessages[j]->Release();
        }



    }
    return 0;
}
