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

#ifdef COPLAY_USE_LOBBIES
// This is mostly a convar for easier testing, dont set the COPLAY_USE_LOBBIES conditional in your vpc if you want this off
ConVar coplay_use_lobbies("coplay_use_lobbies", "1", FCVAR_HIDDEN, "Use Steam Lobbies for connections.\n");
#endif
ConVar coplay_debuglog_steamconnstatus("coplay_debuglog_steamconnstatus", "0", 0, "Prints more detailed steam connection statuses.\n");
ConVar coplay_debuglog_lobbyupdated("coplay_debuglog_lobbyupdated", "0", 0, "Prints when a lobby is created, joined or left.\n");
ConVar coplay_autoopen("coplay_autoopen", "1", FCVAR_ARCHIVE, "Open game for listening on local server start");
extern ConVar coplay_joinfilter;

CCoplaySystem::CCoplaySystem() : CAutoGameSystemPerFrame("CoplaySystem")
{
	m_oldConnectCallback = NULL;
	s_instance = this;
	m_role = eConnectionRole_INACTIVE;
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
    SetRole(eConnectionRole_INACTIVE);
}

static void ConnectOverride(const CCommand& args)
{
    CCoplaySystem::GetInstance()->CoplayConnect(args);
}

FnCommandCallback_t g_oldDisconnectCallback;
static void DisconnectOverride(const CCommand& args)
{
    g_oldDisconnectCallback(args);
    if (CCoplaySystem::GetInstance()->GetRole() == eConnectionRole_CLIENT)
        CCoplaySystem::GetInstance()->GetClient()->CloseConnection();

}

void CCoplaySystem::PostInit()
{
    // Some cvars we need on
    ConVarRef net_usesocketsforloopback("net_usesocketsforloopback");// allows connecting to 127.* addresses
    net_usesocketsforloopback.SetValue(true);

	// replace the connect command with our own
	ConCommand *connectCommand = g_pCVar->FindCommand("connect");
	if (connectCommand)
	{
		// member variable offset magic
		// this offset should be the same on the SP, MP and Alien Swarm branches. If you're on something older, sorry.
		m_oldConnectCallback = *(FnCommandCallback_t*)((intptr_t)(connectCommand)+0x18);
		*(FnCommandCallback_t*)((intptr_t)(connectCommand)+0x18) = ConnectOverride;
	}
	ConCommand *disconnectCommand = g_pCVar->FindCommand("disconnect");
	if (disconnectCommand)
	{
		g_oldDisconnectCallback = *(FnCommandCallback_t*)((intptr_t)(disconnectCommand)+0x18);
		*(FnCommandCallback_t*)((intptr_t)(disconnectCommand)+0x18) = DisconnectOverride;
	}

}

void CCoplaySystem::Update(float frametime)
{
    SteamAPI_RunCallbacks();
    GetHost()->Update();

#ifndef COPLAY_DONT_UPDATE_RPC

    // Connect Steam RPC
    static float lastupdated = 0;
    if (lastupdated + 1 < gpGlobals->realtime)
    {
        if (GetHost()->IsHosting() && coplay_joinfilter.GetInt() != eP2PFilter_CONTROLLED)
        {
            std::string connect = "+" + GetConnectCommand();
            SteamFriends()->SetRichPresence("connect", connect.c_str());
            SteamFriends()->SetRichPresence("coplay_playercount", std::to_string(GetHost()->GetConnectionCount()+1).c_str());
        }
        else if (GetRole() == eConnectionRole_INACTIVE && engine->IsConnected()) // On normal server
        {
            INetChannelInfo *netinfo = engine->GetNetChannelInfo();
            std::string connect = "+connect ";
            connect += netinfo->GetAddress();
            SteamFriends()->SetRichPresence("connect",connect.c_str());
            SteamFriends()->SetRichPresence("coplay_playercount", "");
        }
        else
        {
            SteamFriends()->SetRichPresence("connect", "");
            SteamFriends()->SetRichPresence("coplay_playercount", "");
        }
        lastupdated = gpGlobals->realtime;
    }
#endif
}

void CCoplaySystem::LevelInitPostEntity()
{
    // ensure we're in a local game
    INetChannelInfo* netinfo = engine->GetNetChannelInfo();
    const char* addr = netinfo->GetAddress();
    if (!(netinfo->IsLoopback() || !V_strncmp(addr, "127", 3)))
        return;

	// start hosting if we arent already a client or host
	if (m_role == eConnectionRole_INACTIVE && coplay_autoopen.GetBool())
		SetRole(eConnectionRole_HOST);
}

void CCoplaySystem::LevelShutdownPreEntity()
{
	if (!engine->IsConnected())
		SetRole(eConnectionRole_INACTIVE);

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
		GetHost()->StopHosting();
		break;
	case eConnectionRole_CLIENT:
		GetClient()->CloseConnection();
		break;
    default:
        break;
	}

	// start new role
    switch (role)
    {
	case eConnectionRole_HOST:
		GetHost()->StartHosting();
		break;
    }

	m_role = role;
}

void CCoplaySystem::ConnectToHost(CSteamID host, std::string passcode)
{
	SetRole(eConnectionRole_CLIENT);
	GetClient()->ConnectToHost(host, passcode);
}

void CCoplaySystem::ConnectionStatusUpdated(SteamNetConnectionStatusChangedCallback_t* pParam)
{
	bool stateFailed = false;
	//Msg("%i %i\n", pParam->m_info.m_eState, pParam->m_info.m_eEndReason);
    switch(m_role)
    {
	case eConnectionRole_HOST:
		stateFailed = GetHost()->ConnectionStatusUpdated(pParam);
		break;
	case eConnectionRole_CLIENT:
		stateFailed = GetClient()->ConnectionStatusUpdated(pParam);
		break;
	default:
		break;
	}

	// the role is no longer active so return to the disconnected state
	if (stateFailed)
		SetRole(eConnectionRole_INACTIVE);
}

void CCoplaySystem::LobbyJoined(LobbyEnter_t* pParam)
{
    if (pParam->m_EChatRoomEnterResponse != k_EChatRoomEnterResponseSuccess
        || (GetRole() == eConnectionRole_HOST && GetHost()->GetLobby() == pParam->m_ulSteamIDLobby))
        return;

	// we've joined the lobby so attempt to connect to the host
	ConnectToHost(CSteamID(pParam->m_ulSteamIDLobby));
}

void CCoplaySystem::LobbyJoinRequested(GameLobbyJoinRequested_t *pParam)
{
	SteamMatchmaking()->JoinLobby(pParam->m_steamIDLobby);
}

void CCoplaySystem::JoinGame(GameRichPresenceJoinRequested_t *pParam)
{
	if (!pParam->m_rgchConnect)
		return;

	std::string command = pParam->m_rgchConnect;
	// People could put anything they want in the steam rich presence if they wanted to. Check its what we expect before running
	if (command.empty() || (command.find("+connect") != 0 && command.find("+coplay_connect") != 0)
		|| command.find_first_of("\'\"\\/;") != std::string::npos )
	{
		ConColorMsg(COPLAY_DEBUG_MSG_COLOR, "[Coplay Warning] Got a bad join string ( %s ) "
											"\nMake sure you trust whomever you're trying to connect to and that you are both on the same version of the game.\n",
												command.c_str());
		return;
	}
	engine->ClientCmd_Unrestricted(command.c_str()+1);

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

    std::string destination = args.Arg(1);
    std::string reason = args.Arg(args.ArgC()-1);
    // Might need to send password later
    //if (!coplay_use_lobbies.GetBool())
    //    m_password = std::string(args.Arg(2));

	// if we're already connected, disconnect


    if (destination.find_first_of('.', 0) != std::string::npos || // normal server, probably
        destination.compare("localhost") == 0) // our own server
    {
        if (reason != "coplay")
            SetRole(eConnectionRole_INACTIVE);
        // call the old connect command
        if (m_oldConnectCallback)
            m_oldConnectCallback(args);
        else
        {
            // if we're not overriding for some reason, just call the normal connect command
            engine->ClientCmd_Unrestricted(args.GetCommandString());
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
		SetRole(eConnectionRole_INACTIVE);
        engine->ClientCmd_Unrestricted("disconnect");//mimic normal connect behavior
    }

    uint64 id = std::stoull(destination);
    CSteamID steamid(id);
    if (UseCoplayLobbies() && steamid.IsLobby())
    {
		// we have to join the lobby before we can connect to the host
		ConColorMsg(COPLAY_MSG_COLOR, "[Coplay] Attempting to join lobby with ID %s....\n", destination.c_str());
        SteamMatchmaking()->JoinLobby(steamid);
        return;
    }

	// if not a lobby, just connect to the host
	if (steamid.BIndividualAccount())
    {
        ConnectToHost(steamid, args.Arg(2));
        return;
    }
    Warning("Coplay_Connect was called with an invalid SteamID! ( %llu )\n", steamid.ConvertToUint64());
}

void CCoplaySystem::OpenSocket(const CCommand& args)
{
    SetRole(eConnectionRole_HOST);
}

void CCoplaySystem::CloseSocket(const CCommand& args)
{
    SetRole(eConnectionRole_INACTIVE);
}

void CCoplaySystem::OnListLobbiesCmd(LobbyMatchList_t *pLobbyMatchList, bool IOFailure)
{
    ConColorMsg(COPLAY_MSG_COLOR, "Available Lobbies:\n");
    ConColorMsg(COPLAY_MSG_COLOR, "%-32s | %-16s | %-19s | Player Count\n", "Hostname", "Map", "ID");
    for (int i = 0; i < pLobbyMatchList->m_nLobbiesMatching; i++)
    {
        CSteamID lobby = SteamMatchmaking()->GetLobbyByIndex(i);
        ConColorMsg(COPLAY_MSG_COLOR, "%-32s | %-16s | %-19llu | %2i/%2i\n",
                                       SteamMatchmaking()->GetLobbyData(lobby, "hostname"),
                                       SteamMatchmaking()->GetLobbyData(lobby, "map"),
                                       lobby.ConvertToUint64(),
                                       SteamMatchmaking()->GetNumLobbyMembers(lobby), SteamMatchmaking()->GetLobbyMemberLimit(lobby) );
    }
}

void CCoplaySystem::PrintAbout(const CCommand& args)
{
    ConColorMsg(COPLAY_MSG_COLOR, "Coplay provides an implementation of Steam Networking within the Source SDK. Visit the Github page for more information and source code\n");
    ConColorMsg(COPLAY_MSG_COLOR, "https://github.com/CoaXioN-Games/coplay\n\n");
    ConColorMsg(COPLAY_MSG_COLOR, "The loaded Coplay version is %s.\nBuilt on %s at %s GMT-0.\n\n", COPLAY_VERSION, __DATE__, __TIME__);

    ConColorMsg(COPLAY_MSG_COLOR, "Active Coplay build options:\n");
#ifdef COPLAY_USE_LOBBIES
     ConColorMsg(COPLAY_MSG_COLOR, " - COPLAY_USE_LOBBIES\n");
#endif
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

std::string CCoplaySystem::GetConnectCommand()
{
    std::string cmd = "";
    if (m_role != eConnectionRole_HOST)
        return cmd;


    uint64 id;
    if (UseCoplayLobbies())
    {
        id = GetHost()->GetLobby().ConvertToUint64();
    }
    else
    {
        SteamNetworkingIdentity netID;
        SteamNetworkingSockets()->GetIdentity(&netID);
        id = netID.GetSteamID64();
    }

    if (coplay_joinfilter.GetInt() == eP2PFilter_CONTROLLED && !UseCoplayLobbies())
        cmd = "coplay_connect " + std::to_string(id) + " " + GetHost()->GetPasscode();
    else
        cmd = "coplay_connect " + std::to_string(id);

    return cmd;
}

void CCoplaySystem::InvitePlayer(const CCommand& args)
{
    if (m_role != eConnectionRole_HOST
        || (UseCoplayLobbies() && GetHost()->GetLobby().ConvertToUint64() == 0) )
    {
        ConColorMsg(COPLAY_MSG_COLOR, "You're not currently hosting a game joinable by Coplay.\n");
        return;
    }

    if (UseCoplayLobbies() && coplay_joinfilter.GetInt() != eP2PFilter_EVERYONE)
    {
        if (GetHost()->GetLobby().ConvertToUint64() == 0)
        {
            ConColorMsg(COPLAY_MSG_COLOR, "You aren't in a lobby.\n");
            return;
        }
        SteamFriends()->ActivateGameOverlayInviteDialog(GetHost()->GetLobby());
        return;
    }
    else
    {
        std::string cmd = GetConnectCommand();
        g_pVGuiSystem->SetClipboardText(cmd.c_str(), cmd.length());
        ConColorMsg(COPLAY_MSG_COLOR, "\n%s\nCopied to clipboard.", cmd.c_str());
        return;
    }
}

void CCoplaySystem::PrintStatus(const CCommand& args)
{
    char *role;
    int count;
    if (m_role == eConnectionRole_CLIENT)
    {
        role = "Client";
        count = GetClient()->IsConnected() ? 1 : 0;
    }
    else if (m_role == eConnectionRole_HOST)
    {
        role = "Hosting";
        count = GetHost()->GetConnectionCount();
    }
    else
    {
        role = "Inactive";
        count = 0;
    }
    Msg("Role: %s\nConnection Count: %i\n", role, count);
}

#ifdef COPLAY_USE_LOBBIES
void CCoplaySystem::ConnectToLobby(const CCommand& args)
{
    // Steam appends '+connect_lobby (id64)' to launch options when boot joining
    std::string cmd = "coplay_connect ";
    cmd += args.ArgS();
    engine->ClientCmd_Unrestricted(cmd.c_str());
}

void CCoplaySystem::ListLobbies(const CCommand& args)
{
    SteamAPICall_t apiCall = SteamMatchmaking()->RequestLobbyList();
    m_lobbyListResult.Set(apiCall, this, &CCoplaySystem::OnListLobbiesCmd);
}
#endif

void CCoplaySystem::ReRandomizePassword(const CCommand& args)
{
    if (GetRole() != eConnectionRole_HOST)
    {
        ConColorMsg(COPLAY_MSG_COLOR, "You're not currently hosting a game.");
        return;
    }
    GetHost()->RandomizePasscode();
}
