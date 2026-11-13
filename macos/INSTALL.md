# BG3 WASD Everywhere for macOS

Direct character movement with WASD for the native macOS build of Baldur's Gate 3 on Apple Silicon
Macs.

Supported game version: **4.1.1.7631656** (Hotfix #36). On any other version, the mod writes an
error to its log and does nothing.

## Install

1. Extract the archive to a folder whose path has no spaces, for example `~/bg3-wasd-everywhere`.
2. The files are not notarized by Apple, so macOS won't run them as downloaded. In Terminal, remove
   the download mark from the folder, with your path:

   ```bash
   xattr -dr com.apple.quarantine ~/bg3-wasd-everywhere
   ```

3. In Steam, open Baldur's Gate 3 → Properties → General → Launch Options and enter, with your
   path:

   ```
   /Users/you/bg3-wasd-everywhere/bg3-wasd-everywhere-launch %command%
   ```

   Steam for macOS can't set environment variables in launch options, so the launcher loads the
   mod into the game and keeps the Steam overlay working.
4. Start the game with Play. On the first start, macOS asks whether Steam may access the Documents
   folder. Allow it: the game keeps its profiles and saves there.
5. The log `~/Library/Logs/bg3-wasd-everywhere/bg3-wasd-everywhere.log` should contain
   `WASD movement enabled`.

On start, the mod binds character movement to your camera movement keys (W/A/S/D and the arrows by
default). It does this in `inputconfig_p1.json` of every player profile and keeps the original as
`inputconfig_p1.json.bg3-wasd-everywhere.bak`.

## Use

- Caps Lock switches between Character and Camera movement.
- Shift+W toggles autoforward.
- Hotkeys, walking speed, mouselook and the rotation cursor are set in `bg3-wasd-everywhere.toml`
  next to `libbg3-wasd-everywhere.dylib`. The mod reloads it about a second after you save it. On a
  Mac keyboard, `alt` is Option and `gui` is Command.
- Don't replace the mod's files while the game is running.

## Uninstall

Clear the launch options. To get back the key bindings you had before the mod, replace
`inputconfig_p1.json` with `inputconfig_p1.json.bg3-wasd-everywhere.bak` in each profile folder
under `~/Documents/Larian Studios/Baldur's Gate 3/PlayerProfiles/`.

## More

Features, known issues and the source code: https://github.com/xzert/BG3-WASD-everywhere

A fork of [BG3WASD](https://github.com/Ch4nKyy/BG3WASD) by Ch4nKyy, with the camera tweaks of
[Native Camera Tweaks](https://github.com/ersh1/BG3_NativeCameraTweaks) by ersh1. Licensed under
GPL-3.0; see `LICENSE`. Not affiliated with Larian Studios.
