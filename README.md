<p align="center"> <img width="500" alt="Coplay Logo" src="https://coaxion.games/res/coplayLogo.svg"/> </p>

Logo made by FLARE145.

# Coplay
A MPL2 licensed Source Engine Multiplayer SDK addon that enables the use of the Steam Datagram Relay with support for Linux and Windows.

If your mod implements Coplay please redirrect issue reports related to it [here](https://github.com/CoaXioN-Games/coplay/issues/new/choose).

## Steam Lobbies
Coplay provides a bare bones implementation for Steam's Matchmaking Lobbies that you can enable if desired. You should only do so if:
1. Your mod has its own unique Steam appid.
2. Your mod does not plan to use lobbies for anything other than server enumeration.

Otherwise nothing will work or you will be dissapointed.

## Usage

| Command | Description | Usage |
| :----- | :--------- | :--- |
| coplay_about | Links to this Github page and prints the current version and enabled build options | `coplay_about` |
| coplay_connect | Connect to a server with a Ipv4 address or SteamID64 (and passcode) | `coplay_connect (IP or SteamID64[passcode])` |
| coplay_opensocket | Allows your game to be joined via Coplay, this is automatically called on server creation if coplay_autoopen is 1 | `coplay_opensocket` |
| coplay_closesocket | Disables your game from being joined via Coplay, this will also kick currently connected players | `coplay_closesocket` |
| coplay_listlobbies* | List joinable lobbies | `coplay_listlobbies` |
| coplay_invite | Either prints and copies to your clipboard a command others can use to connect to your game or brings up the Steam invite dialog if using Coplay Lobbies | `coplay_invite` |


| Cvar | Description | Default value |
| :-- | :--------- | :----------- |
| coplay_autoopen | Will automatically start listening for incoming Steam connections on map start if set | 1 |
| coplay_joinfilter | Sets who is allowed to join the game. -1: Off, 0: Require random passcode, 1: Friends only, 2: Anyone | 1 |
| coplay_timeoutduration | How long in seconds to keep a connection around that has no game activity | 5 |
| coplay_portrange_begin * | Where to start looking for ports to bind on | 3600 |
| coplay_portrange_end * | Where to stop looking for ports to bind on | 3700 |
| coplay_connectionthread_hz | Number of times to service connections per second, it's unlikely you'll need to change this | 300 |

\* :  Only change this if issues arise, a range of at least 64 is recommended.

# Adding to your mod

## Updating the Steamworks SDK
A more updated Steamworks SDK than the one that comes with the Source SDK is required to use Coplay.

Steamworks SDK version above [157](https://partner.steamgames.com/downloads/steamworks_sdk_157.zip) require more work to implement into the Source SDK, if you wish to use the [latest](https://partner.steamgames.com/downloads/steamworks_sdk_161.zip) you need to

1. Remove all references to `CSteamAPIContext`/`g_SteamAPIContext`/`steamapicontext` and replace with the new global accessor equivelent (something like `steamapicontext->SteamFriends()->...` turns to `SteamFriends()->...`.)

2. Remove clientsteamcontext.(cpp/h) from client_base.vpc.

3. Remove all calls to SteamAPI_Init().

After you might have done that you can:

1. Delete the `public/steam` folder of your mod's source tree and replace it with `public/steam` folder inside the downloaded zip, you're safe to delete the contained `lib` folder if you want.

2. Replace the steam_api.lib and libsteam_api.so files found in `lib/public` and `lib/public/linux32` with the ones in the zip under the folder `redistributable_bin`. Make sure to copy the 32 bit versions.

3. Rerun your VPC script and build. Any remaining errors are up to you to fix but the SDK doesn't have many by default.


## Adding Coplay

1. First Either clone Coplay as a git submodule, or download it as a zip then place it into the root of your mod's source( Where folders such as `devtools`, `game` and `public` are. )

2. Open your mod's client .vpc file, and add the line
`$Include "$SRCDIR\coplay\src\coplay.vpc"`
to it somewhere at the top.

3. Rerun your VPC script and build.

4. If you have linker errors when building on Linux delete the libSDL2.so found in the `src/lib/public/linux32` folder and retry.

5. Add the SDL2_net.dll and libSDL2_net.so (if your mod supports Linux) found in coplay/lib to you mod's /bin folder.

### Additional VPC Options

All these options are off by default.
| Name | Function |
| :-- |  :------ |
| COPLAY_USE_LOBBIES | Enable Coplay's lobby implementation mentioned above. |
| COPLAY_DONT_UPDATE_RPC | Disables Coplay updating Steam Rich Presence for the key "connect", if you would rather use your own implementation. |
| COPLAY_DONT_LINK_SDL2 |  Disables Coplay's linking to SDL2, for if you already bind to it elsewhere. |
| COPLAY_DONT_LINK_SDL2_NET | Same as above but for SDL2_net. |

To use these options place `$Conditional OPTION_NAME "1"` above the Coplay `$Include` for each one you want to enable. ( ex.`$Conditional COPLAY_USE_LOBBIES "1"` )

# FAQ

## How?
Coplay is a network relay that maps ports on your local machine to Steam datagram connections with some bells and whistles to make the expirence smoother than an external program.

## My game isn't in the server browser!
Thats not what this claims to do.

## Does custom content work?
Yes, as normal.

## My mod wont launch anymore! "Can't load library client"
Reading all of this README is recomended.

## Can I DM a contributor on Discord for support?
You will most likely be refered to this page if you do. If you have found an undocumented bug open an issue.

## Whats your favorite color?
Green, thanks for asking.
