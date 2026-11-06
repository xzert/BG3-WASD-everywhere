# BG3 WASD Everywhere for macOS: prototype

An early port for the native macOS game on Apple Silicon. Supported game version: 4.1.1.7631656.

## What works

- The movement keys move the character, like `WASDUnlockPatch` on Windows and on Linux. The game
  runs its gamepad movement task (`ecl::CharacterTask_MoveController`) only while its
  controller-mode flag is set; the library turns that check into a `nop`.
- The movement keys are bound in `inputconfig_p1.json` of every player profile in
  `~/Documents/Larian Studios/Baldur's Gate 3/PlayerProfiles/`. It uses the same patcher as the Linux
  port, and keeps the original as `inputconfig_p1.json.bg3-wasd-everywhere.bak` before changing it.
- The macOS game keeps its function names, so the library finds code by name instead of byte
  signatures, then finds the instructions it patches by their pattern (`src/patterns.cpp`).

Not yet: hotkeys, Character/Camera movement mode, stopping the movement keys from panning the
camera, autoforward, walking, the rotation cursor and mouselook.

The library also logs what the next steps need:
- the raw camera input events, through the `ecl::CameraSystem::OnInputEvent` vtable slot;
- key and mouse button events, through a swizzled `-[BG3Application sendEvent:]`.

With `BG3_WASD_CODE_PROBE=1` it also logs calls to `GetRotatedInput` through an inline hook.

## Build

Needs the Xcode Command Line Tools (`xcode-select --install`). The `macos/` folder builds files of
the Linux port (`../linux/src/log.cpp` and `inputconfig.cpp`), so keep the two folders next to each
other.

```bash
make
```

This builds `build/libbg3-wasd-everywhere.dylib` (arm64) and `build/symtest`.

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
It runs as "Baldur's Gate 3" with the library preloaded, once without and once with the code probe,
and checks that every hooked call still returns the right value. `HOME` points at a scratch folder,
so the real log and key bindings are not touched.

## Run

Clear the game's launch options in Steam. Then, with Steam running, start the game from Terminal:

```bash
./run-game.sh
```

Steam for macOS can't set environment variables in launch options. With
`DYLD_INSERT_LIBRARIES=… %command%` it fails with "OS Error 260": it opens the whole line as one
file. So `run-game.sh` starts the game's executable itself, with the library preloaded and
`SteamAppId` set so that the game doesn't ask Steam to start it again. Steam still tracks the game.

The log is `~/Library/Logs/bg3-wasd-everywhere/bg3-wasd-everywhere.log`. `run-game.sh` removes it
before each start.

## Files

- `src/probe.mm`: the library: the WASD unlock, the key bindings and the probes.
- `src/patterns.cpp`: finds the instructions to patch inside the game functions.
- `src/stubs.S`: pass-through stubs. They keep every argument register, so a hook works whatever the
  function's signature is.
- `src/macho.cpp`: the symbol table reader, for a loaded image or a file.
- `src/arm64.cpp`: instruction decoding for patterns and inline hooks.
- `src/symbols.hpp`: the game symbols the library uses.
- `tests/symtest.cpp`: runs the same lookups on the game's executable.
- `tests/standin.mm`, `tests/run-standin.sh`: `make check`.
