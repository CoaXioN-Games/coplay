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
#include "coplay_system.h"
#include <inetchannel.h>
#include <inetchannelinfo.h>
#include <steam/isteamgameserver.h>
#include <vgui/ISystem.h>
#include <tier3/tier3.h>

std::string queuedcommand;
static CCoplaySystem g_CoplaySystem;
CCoplaySystem* CCoplaySystem::s_instance = nullptr;

ConVar coplay_debuglog_steamconnstatus("coplay_debuglog_steamconnstatus", "0", 0, "Prints more detailed steam connection statuses.\n");
ConVar coplay_debuglog_lobbyupdated("coplay_debuglog_lobbyupdated", "0", 0, "Prints when a lobby is created, joined or left.\n");
ConVar coplay_use_lobbies("coplay_use_lobbies", "1", 0, "Use Steam Lobbies for connections.\n");

CCoplaySystem::CCoplaySystem() : CAutoGameSystemPerFrame("CoplaySystem")
{
	m_oldConnectCallback = NULL;
	s_instance = this;
	m_role = eConnectionRole_NOT_CONNECTED;
}

CCoplaySystem* CCoplaySystem::GetInstance()
{
	return s_instance;
}

bool CCoplaySystem::Init()
{
    ConColorMsg(COPLAY_MSG_COLOR, "[Coplay] Initialization started...\n");

    if (SDL_Init(0))
    {
        Error("SDL Failed to Initialize: \"%s\"", SDL_GetError());
    }

    if (SDLNet_Init())
    {
        Error("SDLNet Failed to Initialize: \"%s\"", SDLNet_GetError());
    }

    SteamNetworkingUtils()->InitRelayNetworkAccess();
    return true;
}

void CCoplaySystem::Shutdown()
{
}

static void ConnectOverride(const CCommand& args)
{
    CCoplaySystem::GetInstance()->CoplayConnect(args);
}

void CCoplaySystem::PostInit()
{
    // Some cvars we need on
    ConVarRef net_usesocketsforloopback("net_usesocketsforloopback");// allows connecting to 127.* addresses
    net_usesocketsforloopback.SetValue(true);

    ConVarRef cl_clock_correction("cl_clock_correction");
    cl_clock_correction.SetValue(false);

	// replace the connect command with our own
    ConCommand* connectCommand = g_pCVar->FindCommand("connect");
    if (!connectCommand)
        return;

	// member variable offset magic
	// this offset should be the same on the SP, MP and Alien Swarm branches. If you're on something older sorry.
    m_oldConnectCallback = *(FnCommandCallback_t*)((intptr_t)(connectCommand)+0x18);
    *(FnCommandCallback_t*)((intptr_t)(connectCommand)+0x18) = ConnectOverride;
}

void CCoplaySystem::Update(float frametime)
{
    SteamAPI_RunCallbacks();
}

void CCoplaySystem::LevelInitPostEntity()
{
    // ensure we're in a local game
    INetChannelInfo* netinfo = engine->GetNetChannelInfo();
    const char* addr = netinfo->GetAddress();
	if (!netinfo->IsLoopback() || V_strncmp(addr, "127", 3))
        return;

	// start hosting
    SetRole(eConnectionRole_HOST);
}

void CCoplaySystem::LevelShutdownPreEntity()
{
	SetRole(eConnectionRole_NOT_CONNECTED);
}

void CCoplaySystem::SetRole(ConnectionRole role)
{
	// no role change
    if (m_role == role)
        return;

    // end previous role
    switch(m_role)
    {
	case eConnectionRole_HOST:
		m_host.StopHosting();
		break;
	case eConnectionRole_CLIENT:
		m_client.CloseConnection();
		break;
    default:
        break;
	}

	// start new role
    switch (role)
    {
	case eConnectionRole_HOST:
		m_host.StartHosting();
		break;
    }

	m_role = role;
}

void CCoplaySystem::ConnectToHost(CSteamID host)
{
	SetRole(eConnectionRole_CLIENT);
	m_client.ConnectToHost(host);
}

void CCoplaySystem::ConnectionStatusUpdated(SteamNetConnectionStatusChangedCallback_t* pParam)
{
	bool stateFailed = false;
    switch(m_role)
    {
	case eConnectionRole_HOST:
        stateFailed = m_host.ConnectionStatusUpdated(pParam);
		break;
	case eConnectionRole_CLIENT:
        stateFailed = m_client.ConnectionStatusUpdated(pParam);
		break;
	default:
		break;
	}

	// the role is no longer active so return to the disconnected state
	if (stateFailed)
		SetRole(eConnectionRole_NOT_CONNECTED);
}

void CCoplaySystem::LobbyJoined(LobbyEnter_t* pParam)
{
    if (pParam->m_EChatRoomEnterResponse != k_EChatRoomEnterResponseSuccess)
        return;

	// we've joined the lobby so attempt to connect to the host
	ConnectToHost(CSteamID(pParam->m_ulSteamIDLobby));
}

// ================================================================================================
// 
// Coplay commands
//
// ================================================================================================
void CCoplaySystem::CoplayConnect(const CCommand& args)
{
    if (args.ArgC() < 1)
        return;

    std::string Id = args.Arg(1);
    // Might need to send password later
    //if (!coplay_use_lobbies.GetBool())
    //    m_password = std::string(args.Arg(2));

	// if we're already connected, disconnect


    if (Id.find_first_of('.', 0) != std::string::npos || // normal server, probably
        Id.compare("localhost") == 0) // our own server
    {
        SetRole(eConnectionRole_NOT_CONNECTED);
        // call the old connect command
        if (m_oldConnectCallback)
            m_oldConnectCallback(args);
        else
        {
            // if we're not overriding for some reason, just call the normal connect command
            std::string cmd = "connect " + Id;
            engine->ClientCmd_Unrestricted(cmd.c_str());
        }
        return;
    }

    // what you're here for
    if (SteamNetworkingUtils()->GetRelayNetworkStatus(nullptr) != k_ESteamNetworkingAvailability_Current)
    {
        Warning("[Coplay Warning] Can't Connect! Connection to Steam Datagram Relay not yet established.\n");
        // Game is probably just starting, queue the command to be run once the Steam network connection is established
        queuedcommand = std::string(args.GetCommandString());
        return;
    }

    if (engine->IsConnected())
    {
		// disconnect from current game
        SetRole(eConnectionRole_NOT_CONNECTED);
        engine->ClientCmd_Unrestricted("disconnect");//mimic normal connect behavior
    }

	uint64 id = strtoull(Id.c_str(), NULL, 10);
    CSteamID steamid(id);
    if (coplay_use_lobbies.GetBool() && steamid.IsLobby())
    {
		// we have to join the lobby before we can connect to the host
        ConColorMsg(COPLAY_MSG_COLOR, "[Coplay] Attempting to join lobby with ID %s....\n", Id.c_str());
        SteamMatchmaking()->JoinLobby(steamid);
        return;
    }

	// if not a lobby, just connect to the host
    if (steamid.BIndividualAccount())
    {
        SteamNetworkingIdentity netID;
        netID.SetSteamID64(strtoull(Id.c_str(), NULL, 10));
		ConnectToHost(netID.GetSteamID());
    }
    Warning("Coplay_Connect was called with an invalid SteamID! ( %llu )\n", steamid.ConvertToUint64());
}

#if 0
void CCoplaySystem::OpenSocket(const CCommand& args)
{
    OpenP2PSocket();
}

void CCoplaySystem::CloseSocket(const CCommand& args)
{
    CloseP2PSocket();
}

void CCoplaySystem::ListLobbies(const CCommand& args)
{
    SteamAPICall_t apiCall = SteamMatchmaking()->RequestLobbyList();
    m_lobbyListResult.Set(apiCall, this, &CCoplaySystem::OnLobbyListcmd);
}

void CCoplaySystem::PrintAbout(const CCommand& args)
{
    ConColorMsg(COPLAY_MSG_COLOR, "Coplay allows P2P connections in sourcemods. Visit the Github page for more information and source code\n");
    ConColorMsg(COPLAY_MSG_COLOR, "https://github.com/CoaXioN-Games/coplay\n\n");
    ConColorMsg(COPLAY_MSG_COLOR, "The loaded Coplay version is %s.\nBuilt on %s at %s GMT-0.\n\n", COPLAY_VERSION, __DATE__, __TIME__);

    ConColorMsg(COPLAY_MSG_COLOR, "Active Coplay build options:\n");
#ifdef COPLAY_DONT_UPDATE_RPC
    ConColorMsg(COPLAY_MSG_COLOR, " - COPLAY_DONT_UPDATE_RPC\n");
#endif
#ifdef COPLAY_DONT_LINK_SDL2
    ConColorMsg(COPLAY_MSG_COLOR, " - COPLAY_DONT_LINK_SDL2\n");
#endif
#ifdef COPLAY_DONT_LINK_SDL2_NET
    ConColorMsg(COPLAY_MSG_COLOR, " - COPLAY_DONT_LINK_SDL2_NET\n");
#endif

}

void CCoplaySystem::GetConnectCommand(const CCommand& args)
{
    std::string cmd;
    switch (GetConnectCommand(cmd))
    {
    case 1:
        ConColorMsg(COPLAY_MSG_COLOR, "You're not currently in a game joinable by Coplay.\n");
        break;

    case 2:
        ConColorMsg(COPLAY_MSG_COLOR, "You currently have coplay_joinfilter set to invite only, Use coplay_invite\n");
        break;

    case 0:
        ConColorMsg(COPLAY_MSG_COLOR, "\n%s\nCopied to clipboard.", cmd.c_str());
        g_pVGuiSystem->SetClipboardText(cmd.c_str(), cmd.length());
        break;
    }
}

void CCoplaySystem::ReRandomizePassword(const CCommand& args)
{
    if (GetRole() != eConnectionRole_HOST)
    {
        ConColorMsg(COPLAY_MSG_COLOR, "You're not currently hosting a game.");
        return;
    }
    RechoosePassword();
}

void CCoplaySystem::ConnectToLobby(const CCommand& args)
{
    // Steam appends '+connect_lobby (id64)' to launch options when boot joining
    std::string cmd = "coplay_connect ";
    cmd += args.ArgS();
    engine->ClientCmd_Unrestricted(cmd.c_str());
}

void CCoplaySystem::InviteToLobby(const CCommand& args)
{
    if (GetLobby().ConvertToUint64() == 0)
    {
        ConColorMsg(COPLAY_MSG_COLOR, "You aren't in a lobby.\n");
        return;
    }
    SteamFriends()->ActivateGameOverlayInviteDialog(GetLobby());
}

// Debug commands
void CCoplaySystem::DebugPrintState(const CCommand& args)
{
    ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "Role: %i\nActive Connections: %i\n", GetRole(), m_connections.Count());
}

void CCoplaySystem::DebugCreateDummyConnection(const CCommand& args)
{
    CCoplayConnection* connection = new CCoplayConnection(NULL, GetRole());

    connection->Start();
    m_connections.AddToTail(connection);
}

void CCoplaySystem::ListInterfaces(const CCommand& args)
{
    IPaddress addr[16];
    int num = SDLNet_GetLocalAddresses(addr, sizeof(addr) / sizeof(IPaddress));
    for (int i = 0; i < num; i++)
        Msg("%i.%i.%i.%i\n", ((uint8*)&addr[i].host)[0], ((uint8*)&addr[i].host)[1], ((uint8*)&addr[i].host)[2], ((uint8*)&addr[i].host)[3]);
}

void CCoplaySystem::DebugSendDummySteam(const CCommand& args)
{
	FOR_EACH_VEC(m_connections, i)
	{
		CCoplayConnection* con = m_connections[i];
		if (!con)
			continue;
		char string[] = "Completely Random Test String (tm)";
		int64 msgout;
		SteamNetworkingSockets()->SendMessageToConnection(con->m_hSteamConnection, string, sizeof(string),
			k_nSteamNetworkingSend_UnreliableNoDelay | k_nSteamNetworkingSend_UseCurrentThread,
			&msgout);
	}
}
#endif
