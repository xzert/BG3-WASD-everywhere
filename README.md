# BG3 WASD Everywhere

Direct character movement with WASD (or other keys) for Baldur's Gate 3, on Windows and on the
native Linux build of the game. A macOS port is planned after the Linux port is complete.

This is a fork of [BG3WASD](https://github.com/Ch4nKyy/BG3WASD) by Ch4nKyy
([Nexus Mods](https://www.nexusmods.com/baldursgate3/mods/781)). The Windows mod here is the
original, unchanged.

The fork adds a port for the native Linux version of the game. Larian released that version in
September 2025 (Hotfix #34), and Steam runs it inside the Steam Linux Runtime. The Windows DLL can't
load there, so the port is a separate `LD_PRELOAD` library in [`linux/`](linux/).

## Which version do I need?

| Your setup | Use |
|---|---|
| Windows | The original mod from [Nexus Mods](https://www.nexusmods.com/baldursgate3/mods/781) |
| Linux, game running through Proton (`bin/bg3.exe`) | The original mod with Native Mod Loader. Launch options: `WINEDLLOVERRIDES="bink2w64=n,b" %command%` |
| Linux or Steam Deck, native game (`bin/bg3`, no `.exe`) | The Linux port from this repository |
| macOS | Not supported yet; see [Roadmap](#roadmap) |

To find out which build you have, look in `Baldurs Gate 3/bin`. A `bg3` file without an extension
means the native Linux build.

## Multiplatform port

The Windows version is the original mod. The Linux version is a separate port in
[`linux/`](linux/). A macOS port is planned; see [Roadmap](#roadmap).

The Linux port supports game version **4.1.1.7631656** (Hotfix #36). On any other version it finds
nothing, writes an error to its log and stays inactive. The game itself is not affected. The Linux
port has not been tested on Steam Deck yet.

| Feature | Windows | Linux | macOS |
|---|:---:|:---:|:---:|
| WASD character movement, autoforward, walking, Character/Camera movement mode | ✅ | ✅ | — |
| Movement keys don't pan the camera in Character mode | ✅ | ✅ | — |
| Camera rotation with an invisible or centered cursor | ✅ | ✅ ¹ | — |
| Automatic Camera mode in combat | ✅ | ✅ ⁴ | — |
| `ToggleCameraRotation` (mouselook without holding a button) | ✅ | — | — |
| Rotation turns off in menus, dialogs, loot and photo mode | ✅ | — ² | — |
| No edge panning in Character mode | ✅ | ✅ | — |
| Rotate + left click moves forward | ✅ | ✅ | — |
| Unclickable active character and camera occluders | ✅ | — | — |
| Blocking click-to-move (`BlockInteractMove`) | ✅ | — | — |
| Movement mode follows turn-based mode (FTB) | ✅ | ✅ | — |
| Movement keys bound automatically | ✅ | ✅ ³ | — |
| Config reloads when the file changes | ✅ | ✅ | — |

Notes on the Linux column:

- ¹ `centered` shows the game's rotation icon instead of a crosshair.
- ² The cursor comes back when you release the rotate button.
- ³ Bound when the game starts. If you rebind the camera keys in the game's menu, restart the game
  to apply them to movement.
- ⁴ Combat is detected from the game's combat start and end events. If you load a save in the
  middle of a fight, the mod notices only at the next start or end, so press `ToggleMovementMode`
  yourself.

### Install on Linux

There are no prebuilt releases yet, so build the library first. You need `g++` with C++20 support
and `make`.

1. Build:

   ```bash
   cd linux && make
   ```

   This creates `linux/build/libbg3wasd.so` and the default config `linux/build/BG3WASD.toml`.

2. Check that your game version is supported:

   ```bash
   ./build/selftest ~/.local/share/Steam/steamapps/common/"Baldurs Gate 3"/bin/bg3
   ```

   It should print the addresses it found. `FAILED` means your game version isn't supported yet.

3. In Steam, open Baldur's Gate 3 → Properties → General → Launch Options and enter:

   ```
   LD_PRELOAD="/absolute/path/to/linux/build/libbg3wasd.so:$LD_PRELOAD" %command%
   ```

   The path must not contain spaces.

On start, the mod binds character movement to your camera movement keys (W/A/S/D and the arrows by
default). It does this in `inputconfig_p1.json` of every player profile and keeps the original as
`inputconfig_p1.json.bg3wasd.bak`.

To check that it works, start the game and look at `~/.local/state/BG3WASD/BG3WASD.log`. It should
contain `WASD movement enabled`. Caps Lock switches between Character and Camera movement, and
Shift+W toggles autoforward. Hotkeys, walking speed and the rotation cursor are set in
`BG3WASD.toml` next to the library.

More details, including how the port finds its way around the game's code, are in
[linux/README.md](linux/README.md).

## Roadmap

1. Finish the Linux port: everything marked "—" in the Linux column above.
2. Port the mod to the native macOS version of the game.

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

## Support

If the Linux port is useful to you, you can support its development:

- Boosty: TODO add link
- Tribute: TODO add link

To support the original mod, see Ch4nKyy's
[Nexus Mods page](https://www.nexusmods.com/baldursgate3/mods/781).

## Credits and license

- The original BG3WASD is by [Ch4nKyy](https://github.com/Ch4nKyy/BG3WASD); the Windows mod,
  the reverse engineering guide in [doc/](doc/) and the game knowledge the port builds on are theirs.
- The Linux port is by [xzert](https://github.com/xzert).

Licensed under GPL-3.0, like the original. See [LICENSE](LICENSE). This fork changes the original
by adding the `linux/` port and this README.

Not affiliated with Larian Studios.
