<p align="center"> <img width="500" src="https://coaxion.games/res/coplayLogo.svg"/> </p>

Logo made by FLARE145.

# Coplay
A MPL2 licensed Source Engine Multiplayer SDK addon that enables the use of the Steam Datagram Relay for P2P connections with support for Linux and Windows.

If your mod implements Coplay please redirrect issue reports related to it [here](https://github.com/CoaXioN-Games/coplay/issues/new/choose).

## Usage
Mods implementing Coplay can make their own UI if they want to but there are commands and cvars that come standard.

| Command | Description | Usage |
| :----- | :--------- | :--- |
| coplay_about | Links to this Github page and prints the current version and enabled build options | `coplay_about` |
| coplay_connect | Connect to a server with a Ipv4 address, User SteamID64 or Lobby SteamID64 * | `coplay_connect (IP or SteamID64)` |
| coplay_getconnectcommand | Prints and copies to your clipboard a command others can use to connect to your game | `coplay_getconnectcommand` |
| coplay_opensocket | Allows your game to be joined via Coplay, this is automatically called on server creation if coplay_joinfilter is above -1 | `coplay_opensocket` |
| coplay_closesocket | Disables your game from being joined via Coplay, this will also kick currently connected players | `coplay_closesocket` |
| coplay_listlobbies* | List joinable lobbies | `coplay_listlobbies` |
| coplay_invite* | Brings up the game invite dialog if you're in a game | `coplay_invite` |


| Cvar | Description | Default value |
| :-- | :--------- | :----------- |
| coplay_joinfilter | Sets who is allowed to join the game. -1: Off, 0: Invite Only, 1: Friends only, 2: Anyone(Lobby advertised if available*) | 1 |
| coplay_timeoutduration | How long in seconds to keep a connection around that has no game activity | 5 |
| coplay_portrange_begin ** | Where to start looking for ports to bind on | 3600 |
| coplay_portrange_end ** | Where to stop looking for ports to bind on | 3700 |
| coplay_connectionthread_hz | Number of times to service connections per second, it's unlikely you'll need to change this | 300 |
| coplay_forceloopback | Uses the loopback interface instead of others, only change this if you have issues | 1 |

\* : Only available when Steam Lobbies are, lobbies can only be enabled for mods with an appid on Steam.

\** :  Only change this if issues arise, a range of at least 64 is recommended.

# Adding to your mod

## Prerequistes

### Updating the Steamworks SDK
A more updated Steamworks SDK than the one that comes with the Source SDK is required to use Coplay.

Steamworks SDK version above [157](https://partner.steamgames.com/downloads/steamworks_sdk_157.zip) require more work to implement into the Source SDK, if you wish to use the [latest](https://partner.steamgames.com/downloads/steamworks_sdk_160.zip) you need to

1. Remove all references to `CSteamAPIContext`/`g_SteamAPIContext`/`steamapicontext` and replace with the new global accessor equivelent (something like `steamapicontext->SteamFriends()->...` turns to `SteamFriends()->...`.)

2. Remove clientsteamcontext.(cpp/h) from client_base.vpc.

3. Remove all calls to SteamAPI_Init().

After you might have done that you can:

1. Delete the `public/steam` folder of your mod's source tree and replace it with `public/steam` folder inside the downloaded zip, you're safe to delete the contained `lib` folder if you want.

2. Replace the steam_api.lib and libsteam_api.so files found in `lib/public` and `lib/public/linux32` with the ones in the zip under the folder `redistributable_bin`. Make sure to copy the 32 bit versions.

3. Rerun your VPC script and build. Any remaining errors are up to you to fix but the SDK doesn't have many by default.


### Fix host_thread_mode
Coplay enables this convar on start. Due to an engine bug, locally hosted servers will sometimes run faster than intended. host_thread_mode 2 fixes this but causes inputs by the host player to be discarded because of a desync.
This can be side-stepped by changing this statement in player.cpp CBasePlayer::IsUserCmdDataValid() from
```
bool bValid = ( pCmd->tick_count >= nMinDelta && pCmd->tick_count < nMaxDelta ) &&
				  // Prevent clients from sending invalid view angles to try to get leaf server code to crash
				  ( pCmd->viewangles.IsValid() && IsEntityQAngleReasonable( pCmd->viewangles ) ) &&
				  // Movement ranges
				  ( IsFinite( pCmd->forwardmove ) && IsEntityCoordinateReasonable( pCmd->forwardmove ) ) &&
				  ( IsFinite( pCmd->sidemove ) && IsEntityCoordinateReasonable( pCmd->sidemove ) ) &&
				  ( IsFinite( pCmd->upmove ) && IsEntityCoordinateReasonable( pCmd->upmove ) );
```
to
```
// when using host_thread_mode 1 or 2 on a listen server the client and server tickcount get desynced resulting in none of the hosts usermsgs getting accepted
// this first || fixes that and doesnt seem to do anything else as far as i can tell; Tholp
    bool bValid = ((!engine->IsDedicatedServer() && entindex() == 1) || ( pCmd->tick_count >= nMinDelta && pCmd->tick_count < nMaxDelta )) &&
				  // Prevent clients from sending invalid view angles to try to get leaf server code to crash
				  ( pCmd->viewangles.IsValid() && IsEntityQAngleReasonable( pCmd->viewangles ) ) &&
				  // Movement ranges
				  ( IsFinite( pCmd->forwardmove ) && IsEntityCoordinateReasonable( pCmd->forwardmove ) ) &&
				  ( IsFinite( pCmd->sidemove ) && IsEntityCoordinateReasonable( pCmd->sidemove ) ) &&
				  ( IsFinite( pCmd->upmove ) && IsEntityCoordinateReasonable( pCmd->upmove ) );
```

## Adding Coplay

1. First Either clone Coplay as a git submodule, or download it as a zip then place it into the root of your mod's source( Where folders such as `devtools`, `game` and `public` are. )

2. Open your mod's client .vpc file, and add the line
`$Include "$SRCDIR\coplay\src\coplay.vpc"`
to it somewhere at the top.

3. Rerun your VPC script and build.

4. If you have linker issues when building on Linux delete the libSDL2.so found in your mod's `src/lib/public/linux32` folder and retry.

5. Add the SDL2_net.dll and libSDL2_net.so (if your mod supports Linux) found in coplay/lib to you mod's /bin folder.

### Additional VPC Options

| Name | Function |
| :-- |  :------ |
| COPLAY_USE_LOBBIES | Makes Coplay use Steam's lobby system for managing connections, this allows for games with Coplay enabled to list themselves on Steam. This requires your mod to have an AppID on Steam to function. |
| COPLAY_DONT_UPDATE_RPC | Disables Coplay updating Steam Rich Presence, if you would rather use your own implementation. |
| COPLAY_DONT_LINK_SDL2 |  Disables Coplay's linking to SDL2, for if you already bind to it elsewhere. |
| COPLAY_DONT_LINK_SDL2_NET | Same as above but for SDL2_net. |
| $COPLAY_DONT_SET_THREADMODE | Disables automatically setting host_thread_mode, may be removed later if a better solution for server pacing issues is found |

To use these options place `$Conditional OPTION_NAME "1"` above the Coplay `$Include` for each one you want to enable. ( ex.`$Conditional COPLAY_USE_LOBBIES "1"` )

# FAQ

## How?
Coplay is a network relay that maps ports on your local machine to Steam datagram connections, this allows it to not require modifing engine code.

## Whats the difference if Steam's Lobby system is available?
Lobbies allow user hosted games to be advertised and joined akin to the server browser.

## My game isn't in the server browser!
Thats not what this does, if you are able to use lobbies look at the code for coplay_listlobbies for a starting point on how to make your own menu.

## Does custom content work?
Yes, as normal.

## My mod wont launch anymore! "Can't load library client"
Reading the instructions is recomended.

## I Can't move in my own local server!
Reading the instructions is recomended.

## Can I DM a contributor on Discord for support?
You will most likely be refered to this page if you do. If you have found an undocumented bug open an issue.

## Whats your favorite color?
Green, thanks for asking.
