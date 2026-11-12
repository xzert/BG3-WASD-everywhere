# BG3 WASD Everywhere

## Support

If the Linux or macOS port is useful to you, you can support its development:

- Boosty: TODO add link
- Tribute: TODO add link

## Thanks

The ports stand on the work of two authors. If you enjoy them, thank and support the originals too:

- **Ch4nKyy**, for [BG3WASD](https://github.com/Ch4nKyy/BG3WASD)
  ([Nexus Mods](https://www.nexusmods.com/baldursgate3/mods/781)): the WASD movement, the camera
  rotation and the reverse engineering that the ports build on.
- **ersh1**, for [Native Camera Tweaks](https://github.com/ersh1/BG3_NativeCameraTweaks)
  ([Nexus Mods](https://www.nexusmods.com/baldursgate3/mods/945)): the free camera pitch, zoom and
  the other camera tweaks.

## Trivia

I really liked BG3WASD, until it broke on Linux: the game had moved to a native Linux build. Time
went by, and the Hour Came: everything works on Linux again (and on macOS too).

## About

Direct character movement with WASD (or other keys) for Baldur's Gate 3: on Windows, on the native
Linux build of the game and, as an early prototype, on the native macOS build for Apple Silicon.

This is a fork of [BG3WASD](https://github.com/Ch4nKyy/BG3WASD) by Ch4nKyy
([Nexus Mods](https://www.nexusmods.com/baldursgate3/mods/781)). The Windows mod here is the
original, unchanged.

The fork adds a port for the native Linux version of the game. Larian released that version in
September 2025 (Hotfix #34), and Steam runs it inside the Steam Linux Runtime. The Windows DLL can't
load there, so the port is a separate `LD_PRELOAD` library in [`linux/`](linux/).

The fork also starts a port for the native macOS version on Apple Silicon, in [`macos/`](macos/).
It is a library that a small launcher loads into the game when Steam starts it.

The Linux port also includes the camera tweaks of
[Native Camera Tweaks](https://github.com/ersh1/BG3_NativeCameraTweaks) by ersh1, which most
players use together with BG3WASD: a free camera pitch, wider zoom limits, FOV and offsets.

## Which version do I need?

| Your setup | Use |
|---|---|
| Windows | The original mod from [Nexus Mods](https://www.nexusmods.com/baldursgate3/mods/781) |
| Linux, game running through Proton (`bin/bg3.exe`) | The original mod with Native Mod Loader. Launch options: `WINEDLLOVERRIDES="bink2w64=n,b" %command%` |
| Linux or Steam Deck, native game (`bin/bg3`, no `.exe`) | The Linux port from this repository |
| macOS on Apple Silicon | The early macOS port from this repository; build it yourself, see [Install on macOS](#install-on-macos) |

To find out which build you have, look in `Baldurs Gate 3/bin`. A `bg3` file without an extension
means the native Linux build.

## Multiplatform port

The Windows version is the original mod. The Linux and macOS versions are separate ports in
[`linux/`](linux/) and [`macos/`](macos/); the macOS one is an early prototype.

Both ports support game version **4.1.1.7631656** (Hotfix #36).
- The Linux port finds the game's code by byte signatures. On any other version it finds nothing,
  writes an error to its log and stays inactive; the game itself is not affected. It has not been
  tested on Steam Deck yet.
- The macOS port finds the game's functions by their names, which the macOS build keeps.

| Feature | Windows | Linux | macOS |
|---|:---:|:---:|:---:|
| WASD character movement, autoforward, walking, Character/Camera movement mode | ✅ | ✅ | ✅ |
| Movement keys don't pan the camera in Character mode | ✅ | ✅ | ✅ |
| Camera rotation with an invisible or centered cursor | ✅ | ✅ ¹ | ✅ ¹ |
| Automatic Camera mode in combat | ✅ | ✅ ⁴ | — |
| `ToggleCameraRotation` (mouselook without holding a button) | ✅ | ✅ | ✅ |
| Rotation turns off in menus, dialogs, loot and photo mode | ✅ | ✅ ² | ✅ ² |
| No edge panning in Character mode | ✅ | ✅ | — |
| Rotate + left click moves forward | ✅ | ✅ | ✅ |
| Unclickable active character and camera occluders | ✅ | ✅ ⁵ | ✅ ⁵ |
| Camera tweaks: free pitch, zoom limits, FOV, offsets ([Native Camera Tweaks](https://www.nexusmods.com/baldursgate3/mods/945)) | separate mod | ✅ ⁶ | — |
| Blocking click-to-move (`BlockInteractMove`) | ✅ | — | — |
| Movement mode follows turn-based mode (FTB) | ✅ | ✅ | — |
| Movement keys bound automatically | ✅ | ✅ ³ | ✅ ³ |
| Config reloads when the file changes | ✅ | ✅ | ✅ |

Notes on the Linux column:

- ¹ `centered` shows the game's rotation icon instead of a crosshair.
- ² The Esc menu, dialogs, loading screens and text fields pause mouselook by themselves.
  - Panels such as the inventory, map or journal pause it when you open them with their hotkeys
    (`MouselookPauseKeys`).
  - Mouselook resumes when you close the panel with the same key or Esc, or press the mouselook key.
  - The loot window and photo mode aren't detected.
- ³ Bound when the game starts. If you rebind the camera keys in the game's menu, restart the game
  to apply them to movement.
- ⁴ Combat is detected from the game's combat start and end events. If you load a save in the
  middle of a fight, the mod notices only at the next start or end, so press `ToggleMovementMode`
  yourself.
- ⁵ The active character (`PreventClickingActiveCharacter`) and camera occluders
  (`PreventClickingCameraOccluders`).
- ⁶ Built in and on by default (`CameraTweaks`), with the same config keys as Native Camera Tweaks.
  Not ported yet: the gamepad settings, keeping the camera above the floor and keeping the zoom on a
  zone change. Without the floor check, the free pitch stops at 0 (looking straight ahead) by
  default.

Notes on the macOS column:

- ¹ ² ³ As on Linux. Mouselook's pause in dialogs isn't checked in game yet.
- ⁵ The active character and camera occluders (`PreventClickingCameraOccluders`).
- The config has only the settings the port supports so far. Key names are the same as on Linux;
  on a Mac keyboard, `alt` is Option and `gui` is Command.

### Install on Linux

1. Download `bg3-wasd-everywhere-linux-<version>.tar.gz` from
   [Releases](https://github.com/xzert/BG3-WASD-everywhere/releases) and extract it to a folder whose
   path has no spaces, for example `~/bg3-wasd-everywhere`.
2. In Steam, open Baldur's Gate 3 → Properties → General → Launch Options and enter, with your
   path:

   ```
   LD_PRELOAD="/home/you/bg3-wasd-everywhere/libbg3-wasd-everywhere.so:$LD_PRELOAD" %command%
   ```

The archive also contains these steps in `INSTALL.md`.

To build from source instead, run `make` in `linux/` and point `LD_PRELOAD` at
`linux/build/libbg3-wasd-everywhere.so`; see [linux/README.md](linux/README.md#build). A library
built this way needs your system's glibc version. The release archive is built in the Steam Runtime
SDK and needs only the runtime's glibc 2.31.

On start, the mod binds character movement to your camera movement keys (W/A/S/D and the arrows by
default). It does this in `inputconfig_p1.json` of every player profile and keeps the original as
`inputconfig_p1.json.bg3-wasd-everywhere.bak`.

To check that it works, start the game and look at
`~/.local/state/bg3-wasd-everywhere/bg3-wasd-everywhere.log`. It should contain
`WASD movement enabled`. Caps Lock switches between Character and Camera movement, and Shift+W
toggles autoforward. Hotkeys, walking speed and the rotation cursor are set in
`bg3-wasd-everywhere.toml` next to the library.

More details, including how the port finds its way around the game's code, are in
[linux/README.md](linux/README.md).

### Install on macOS

The macOS port is an early prototype for Apple Silicon Macs, without prebuilt releases yet.

1. Install the Xcode Command Line Tools (`xcode-select --install`), get this repository into a
   folder whose path has no spaces, and build:

   ```bash
   cd macos && make
   ```

2. In Steam, open Baldur's Gate 3 → Properties → General → Launch Options and enter, with your
   path:

   ```
   /Users/you/BG3-WASD-everywhere/macos/build/bg3-wasd-everywhere-launch %command%
   ```

3. Start the game with Play. On the first start, macOS asks whether Steam may access the Documents
   folder. Allow it: the game keeps its profiles and saves there.

The log is `~/Library/Logs/bg3-wasd-everywhere/bg3-wasd-everywhere.log`. Caps Lock switches between
Character and Camera movement, and Shift+W toggles autoforward. Hotkeys, walking speed and the
rotation cursor are set in `macos/build/bg3-wasd-everywhere.toml`, next to the library. More details,
including why Steam needs the launcher, are in [macos/README.md](macos/README.md).

## Roadmap

1. Finish the Linux port: everything marked "—" in the Linux column above.
2. Finish the macOS port: the rest of the table (Camera mode in combat, edge panning, turn-based
   mode) and a release build.

## Building the Windows version

### Requirements

- [CMake](https://cmake.org/)
  - Add this to your `PATH`
- [PowerShell](https://github.com/PowerShell/PowerShell/releases/latest)
- [Vcpkg](https://github.com/microsoft/vcpkg)
  - Add the environment variable `VCPKG_ROOT` with the value as the path to the folder containing
  vcpkg
- [Visual Studio Community 2022](https://visualstudio.microsoft.com/)
  - Desktop development with C++
- [Baldur's Gate 3 Steam Distribution](https://store.steampowered.com/app/1086940/Baldurs_Gate_3/)
  - Add the environment variable `BG3PATH` with the value as path to game install folder
  - Add `BG3PATH2` if you have a secondary installation (E.g. one for Steam, one for GOG)
- [7zip](https://www.7-zip.org/)
  - Install to default dir

### Register Visual Studio as a Generator

- Open `x64 Native Tools Command Prompt`
- Run `cmake`
- Close the cmd window

### Building

```
git clone https://github.com/xzert/BG3-WASD-everywhere.git
cd BG3-WASD-everywhere
git submodule init
git submodule update --remote
.\build-release.ps1
```

### Solution Generation (Optional)

If you want to generate a Visual Studio solution, run the following command:

```
.\generate-sln.ps1
```

> ***Note:*** *This will generate a `BG3WASD.sln` file in the **build** directory.*

### VSCode Intellisense (Optional)

To fix Intellisense in VSCode, do the following:

- Install the extensions `ms-vscode.cpptools` and `ms-vscode.cmake-tools`.
- Build the solution with the cmake tools extension.
- In your `c_cpp_properties.json`, use `"configurationProvider": "ms-vscode.cmake-tools"`.
  This is the only needed parameter, apart from "name".

### More documentation

For more on architecture and reverse engineering, see the [doc](doc/) folder.

## Credits and license

- The original BG3WASD is by [Ch4nKyy](https://github.com/Ch4nKyy/BG3WASD); the Windows mod,
  the reverse engineering guide in [doc/](doc/) and the game knowledge the port builds on are theirs.
- The camera tweaks in the Linux and macOS ports are a port of
  [BG3_NativeCameraTweaks](https://github.com/ersh1/BG3_NativeCameraTweaks) by
  [ersh1](https://github.com/ersh1) (GPL-3.0-or-later); the design and the game knowledge are theirs.
- The Linux and macOS ports are by [xzert](https://github.com/xzert).

Licensed under GPL-3.0, like the original. See [LICENSE](LICENSE). This fork changes the original
by adding the `linux/` and `macos/` ports and this README.

Not affiliated with Larian Studios.
