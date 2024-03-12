//================================================
// CoaXioN Source SDK p2p networking: "CoaXioN Coplay"
// Author : Tholp / Jackson S
// File Last Modified : Mar 8 2024
//================================================

#include "cbase.h"
#include "coplay.h"

int CCoplayPacketHandler::Run()
{
    CoplaySteamSocketTuple *sockettuple;
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
                continue;
            }
        }
    }
    return 0;
}
