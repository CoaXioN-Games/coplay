<p align="center"> <img width="500" src="https://coaxion.games/res/coplayLogo.svg"/> </p>

# Coplay
A MPL2 licensed drop-in Source Engine multiplayer SDK addon that enables the use of the Steam Datagram Relay for P2P connections with support for Linux and Windows.

## Usage
Ideally, mods implementing Coplay should make their own UI but there are commands and cvars that come standard.

| Command | Description | Usage |
| :-----: | :---------: | :---: |
| `coplay_connect` | Connect to a server with a Ipv4 address, User SteamID64 or Lobby SteamID64 * | `coplay_connect (IP or SteamID64)` |
| `coplay_getconnectcommand` | Prints a command for others to use to connect to your server | `coplay_getconnectcommand` |
| `coplay_opensocket` | Allows your game to be joined via Coplay | `coplay_opensocket` |
| `coplay_closesocket` | Disables your game from being joined via Coplay, this will also kick currently connected players | `coplay_closesocket` |
| `coplay_listlobbies`* | List joinable lobbies | `coplay_listlobbies` |


| Cvar | Description | Default value |
| :--: | :---------: | :-----------: |
| `coplay_joinfilter` | Sets who is allowed to join the game. 0: Invite Only, 1: Friends only or Invite, 2: Anyone(Lobby advertised if available)** | 1 |
| `coplay_timeoutduration` | How long in seconds to keep a connection around that has no game activity | 45 |
| `coplay_portrange_begin` *** | Where to start looking for ports to bind on | 3600 |
| `coplay_portrange_end` *** | Where to stop looking for ports to bind on | 3700 |

\* : Lobbies are only available for mods on Steam.
\** : Games requiring an invite are not yet implemented for mods that cant support lobbies.
\*** :  Only change this if issues arise, a range of at least 64 is recommended.

## Adding to your mod

### Updating the Steamworks SDK
A more updated Steamworks SDK than the one that comes with the Source SDK is required to use Coplay, If you haven't already updated your mod's version you can find the latest download [here.](https://partner.steamgames.com/downloads/steamworks_sdk_159.zip)

1. Delete the `public/steam` folder of your mod's source tree and replace it with `public/steam` folder inside the downloaded zip, you're safe to delete the contained `lib` folder if you want.

2. Replace the steam_api.lib and libsteam_api.so files found in `lib/public` and `lib/public/linux32` with the ones in the zip under the folder `redistributable_bin`. Make sure to copy the 32 bit versions.

3. Rerun your VPC script and build, there may errors from existing code being incompatible but the Source SDK doesn't have much by default.

### Adding Coplay

1. First Either clone Coplay as a git submodule, or download it as a zip then place it into the root of your mod's source( Where folders such as `devtools`, `game` and `public` are. )

2. Open your mod's client .vpc file, and add the line
`$Include "$SRCDIR\coplay\src\coplay.vpc"`
to it somewhere at the top, if your mod is on Steam you'll probably want to append `COPLAY_USE_LOBBIES` to your preproccessor definitions.

3. Thats it! Rerun your VPC script and build!

## FAQ

### How?
Coplay is a network relay that maps ports on your local machine to Steam datagram connections, this allows it to not require modifing engine code.

### Whats the difference if Steam's Lobby system is available?
Lobbies allow user hosted games to be advertised and joined akin to the server browser.

### Whats your favorite color?
Green, thanks for asking.
