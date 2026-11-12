# BG3 WASD Everywhere for native Linux

Direct character movement with WASD for the native Linux build of Baldur's Gate 3: the game's
`bin` folder has a `bg3` file without an extension. If your game runs `bin/bg3.exe` through
Proton, use the original Windows mod instead.

Supported game version: **4.1.1.7631656** (Hotfix #36). On any other version, the mod writes an
error to its log and does nothing.

## Install

1. Extract the archive to a folder whose path has no spaces, for example `~/bg3-wasd-everywhere`.
2. In Steam, open Baldur's Gate 3 → Properties → General → Launch Options and enter, with your
   path:

   ```
   LD_PRELOAD="/home/you/bg3-wasd-everywhere/libbg3-wasd-everywhere.so:$LD_PRELOAD" %command%
   ```

   Keeping `$LD_PRELOAD` keeps the Steam overlay working.
3. Start the game. The log `~/.local/state/bg3-wasd-everywhere/bg3-wasd-everywhere.log` should
   contain `WASD movement enabled`.

On start, the mod binds character movement to your camera movement keys (W/A/S/D and the arrows by
default). It does this in `inputconfig_p1.json` of every player profile and keeps the original as
`inputconfig_p1.json.bg3-wasd-everywhere.bak`.

## Use

- Caps Lock switches between Character and Camera movement.
- Shift+W toggles autoforward.
- Hotkeys, walking speed, mouselook and the rotation cursor are set in `bg3-wasd-everywhere.toml`
  next to `libbg3-wasd-everywhere.so`. The mod reloads it a second or two after you save it.
- Don't replace `libbg3-wasd-everywhere.so` while the game is running.

## Uninstall

Remove the launch option. To get back the key bindings you had before the mod, replace
`inputconfig_p1.json` with `inputconfig_p1.json.bg3-wasd-everywhere.bak` in each profile folder
under `~/.local/share/Larian Studios/Baldur's Gate 3/PlayerProfiles/`.

## More

Features, known issues and the source code: https://github.com/xzert/BG3-WASD-everywhere

A fork of [BG3WASD](https://github.com/Ch4nKyy/BG3WASD) by Ch4nKyy. Licensed under GPL-3.0; see
`LICENSE`. Not affiliated with Larian Studios.
