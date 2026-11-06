# BG3 WASD Everywhere for macOS: prototype

An early port for the native macOS game on Apple Silicon. Supported game version: 4.1.1.7631656.

## What works

- The movement keys move the character, like `WASDUnlockPatch` on Windows and on Linux. The game
  runs its gamepad movement task (`ecl::CharacterTask_MoveController`) only while its
  controller-mode flag is set; the library turns that check into a `nop`.
- Caps Lock switches between Character and Camera movement mode, as in the Linux port.
  - In Character mode, the movement keys don't pan the camera: the camera movement actions (ids 99
    to 102) reach `ecl::CameraSystem::OnInputEvent` with id 0.
  - In Camera mode they only pan it: `GetRotatedInput`, the movement input of the movement task, is
    hooked and returns no movement. A gamepad keeps working as without the mod.
- Shift+W toggles autoforward in Character mode. The game gets a key-down of the W key from the
  mod instead of the Shift+W press. Pressing W or S, Shift+W again or switching to Camera mode ends
  it, as in the Linux port.
- The movement keys are bound in `inputconfig_p1.json` of every player profile in
  `~/Documents/Larian Studios/Baldur's Gate 3/PlayerProfiles/`. It uses the same patcher as the Linux
  port, and keeps the original as `inputconfig_p1.json.bg3-wasd-everywhere.bak` before changing it.
- The macOS game keeps its function names, so the library finds code by name instead of byte
  signatures, then finds the instructions it patches by their pattern (`src/patterns.cpp`).

Not yet: the config file and other hotkeys (Caps Lock and Shift+W are fixed for now), walking, the
rotation cursor, mouselook and the other features of the Linux port.

The library also logs what the next steps need:
- the raw camera input events, through the `ecl::CameraSystem::OnInputEvent` vtable slot;
- key and mouse button events, through a swizzled `-[BG3Application sendEvent:]`.

## Build

Needs the Xcode Command Line Tools (`xcode-select --install`). The `macos/` folder builds files of
the Linux port (`../linux/src/log.cpp` and `inputconfig.cpp`), so keep the two folders next to each
other.

```bash
make
```

This builds `build/libbg3-wasd-everywhere.dylib` (arm64), `build/bg3-wasd-everywhere-launch` and
`build/symtest`.

## Check

```bash
./build/symtest ~/Library/"Application Support"/Steam/steamapps/common/"Baldurs Gate 3"/"Baldur's Gate 3.app"/Contents/MacOS/"Baldur's Gate 3"
```

`symtest` finds the game functions the library uses and the instructions it patches, the same way
the library does in memory. It should end with `OK`. It also builds on Linux.

To try the library without the game:

```bash
make check
```

This builds `build/standin`, a small program with the same symbols and vtable slot as the game.
It runs as "Baldur's Gate 3" with the library preloaded. It checks that every hooked call still
returns the right value, and that Caps Lock switches what the camera and the movement task get.
`HOME` points at a scratch folder, so the real log and key bindings are not touched.

## Run

In Steam, open Baldur's Gate 3 → Properties → General → Launch Options and enter:

```
/absolute/path/to/macos/build/bg3-wasd-everywhere-launch %command%
```

Then start the game with Play, as usual.

- **Why a launcher.** Steam for macOS can't set environment variables in launch options. With
  `DYLD_INSERT_LIBRARIES=… %command%` it fails with "OS Error 260": it runs the first word of the
  line as a program.
- **What the launcher does.** It gets the game's `.app` from `%command%` and adds the library to
  `DYLD_INSERT_LIBRARIES`, keeping Steam's own libraries (the overlay). Then it replaces itself with
  the game, so Steam tracks the game as usual.
- **Why not a shell script.** macOS clears `DYLD_*` variables when it starts a system shell, so a
  script would lose Steam's libraries.
- **How it was started.** It writes the way Steam started it to
  `~/Library/Logs/bg3-wasd-everywhere/launch.log`.
- **Documents access.** On the first start, macOS asks whether Steam may access the Documents
  folder. Allow it: the game keeps its profiles and saves there, and macOS counts a game started by
  the launcher as Steam's.

Without the launch options, `./run-game.sh` starts the game from Terminal, with Steam running.

The library's log is `~/Library/Logs/bg3-wasd-everywhere/bg3-wasd-everywhere.log`. The launcher and
`run-game.sh` remove it before each start.

## Files

- `src/probe.mm`: the library: the WASD unlock, the movement modes, the key bindings and the probes.
- `src/launch.cpp`: the launcher for Steam's launch options.
- `src/patterns.cpp`: finds the instructions to patch inside the game functions.
- `src/stubs.S`: pass-through stubs. They keep every argument register, so a hook works whatever the
  function's signature is.
- `src/macho.cpp`: the symbol table reader, for a loaded image or a file.
- `src/arm64.cpp`: instruction decoding for patterns and inline hooks.
- `src/symbols.hpp`: the game symbols the library uses.
- `tests/symtest.cpp`: runs the same lookups on the game's executable.
- `tests/standin.mm`, `tests/run-standin.sh`: `make check`.
