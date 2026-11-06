# BG3 WASD Everywhere for native Linux

An `LD_PRELOAD` library for the native Linux build of Baldur's Gate 3. Steam runs that build inside
the Steam Linux Runtime 3.0 (sniper) container. The Windows mod is a DLL for `bg3.exe` and cannot
load there.

Supported game build: **4.1.1.7631656**. On any other build the library finds no signatures, logs
an error and stays inactive.

## What works

- Direct WASD character movement. The same 13-byte controller-mode check that `WASDUnlockPatch`
  removes on Windows is patched out.
- The movement input override from `OverrideMovementInputHook`: autoforward, walking speed, and
  Character/Camera movement mode.
- Hotkeys and settings from `bg3-wasd-everywhere.toml`: `ToggleMovementMode`, `HoldMovementMode`,
  `ToggleAutoforward`, `ToggleWalkspeed`, `HoldWalkspeed`, `ReloadConfig`, `WalkSpeed`,
  `IsWalkingDefault`, `IsCharacterMovementDefault`.
- In Character movement mode, the movement keys no longer pan the camera. This is the camera move
  part of `ExecuteCameraCommandHook`. It is optional: if its signatures are missing, the log says so
  and movement keeps working.
- Camera rotation with a hidden cursor (`CharacterMovementRotationCursor` and
  `CameraMovementRotationCursor`):
  - `invisible`: while the rotate button is held, the cursor disappears, rotation doesn't stop at
    the screen edge, and the cursor returns to the same spot afterwards. A click without moving the
    mouse stays a click.
  - `centered`: the cursor is held in the center of the screen. It still shows the vanilla rotation
    icon, not a crosshair.
  - `vanilla`: the game's own behavior.
- Movement mode follows combat and forced turn-based mode (FTB): Camera mode when they start,
  Character mode when they end (`AutoToggleMovementMode`, `SwitchToWalkingAfterCombat`). The Windows
  mod reads combat from the camera; this port uses the game's own `combat_start`, `combat_end`,
  `ftb_start` and `ftb_end` telemetry events. A save loaded in the middle of a fight is noticed only
  at the next start or end.
- Rotate + left click moves forward (`RotatePlusLeftclickMovesForward`).
- Mouselook (`ToggleCameraRotation`, `TogglingMovementModeAlsoTogglesCameraRotation`).
  - How it rotates: the Windows mod sends the rotate command to the camera's command handler. This
    port pushes the rotate binding (`CameraToggleMouseRotate`, the middle mouse button by default)
    as an SDL event and holds it down.
  - Automatic pause: when the game stops taking movement input (the Esc menu, loading screens), in
    dialogs (`dialog_start`/`dialog_end` telemetry) and in text fields.
  - Panels: the inventory, map and similar panels keep movement input on, so their hotkeys pause
    mouselook instead. The setting is `MouselookPauseKeys`, Linux only; its default is the game's
    Adventure Panels keys I, M, J, K, N, Tab, B, L, P and H.
  - Resuming: close the panel with the same key or Esc, or press the mouselook key.
- In Character mode outside combat, moving the cursor to the screen edge doesn't pan the camera, and
  the chase camera keeps the character (`BlockEdgePanningHook` and
  `PreventLosingChaseCamTargetHook`). On Linux, `GetViewport` is inlined. The mod rewrites the `jne`
  that skips edge panning with a gamepad into `nop ; jmp`, a two-byte change made with one store,
  and switches it back when the mode or combat changes.
- `bg3-wasd-everywhere.toml` is reloaded about a second after it changes.
- Movement keys are bound automatically, like `InputconfigPatcher` on Windows.
  - What it does: binds `CharacterMoveForward/Backward/Left/Right` to the keys of
    `CameraForward/Backward/Left/Right` (or the defaults), drops extra `key:unknown` entries, and, when
    right click rotates the camera, binds its modifier variants and moves Ping off right click.
  - Where: `inputconfig_p1.json` of every player profile in
    `~/.local/share/Larian Studios/Baldur's Gate 3/PlayerProfiles/`.
  - When: once at startup, before the game reads the file. A file that's already right is not
    rewritten. Before the first change, the original is saved as
    `inputconfig_p1.json.bg3-wasd-everywhere.bak`.
  - Rebinding: the Windows mod re-patches after keybinding changes in the game's menu; here, restart
    the game.
  - Check offline: `./build/selftest --inputconfig <file> <output>`.

## Not ported yet

- From the camera rotation features:
  - mouselook doesn't pause by itself for the loot window and photo mode;
  - the crosshair icon for `centered`.
- Hotkeys still work while menus are open. They are ignored only while you type into a text field.
- No interact-move blocker and no unclickable active character.

## Known issues

- With the game's Edge Panning option on, moving the cursor to the screen edge doesn't pan the camera
  in Camera mode either. In that mode the mod has restored the game's original code (the log shows
  `Edge panning allowed (code now 0f 85)`), so the cause is elsewhere and not yet known. Panning with
  the movement keys works.

## Build

```bash
make
```

This needs `g++` with C++20 and produces:

- `build/libbg3-wasd-everywhere.so`: the mod.
- `build/selftest`: checks the signatures against a game binary without starting the game.
- `build/bg3-wasd-everywhere.toml`: the default config, from `bg3-wasd-everywhere.toml`. It is
  copied once, so later builds keep your edits.

To check a game build:

```bash
./build/selftest ~/.local/share/Steam/steamapps/common/"Baldurs Gate 3"/bin/bg3
```

A library built this way needs the glibc version of the machine it was built on, which can be newer
than what players have. To share a build, use the release build.

### Release build

```bash
./release.sh
```

This builds `dist/bg3-wasd-everywhere-linux-<version>.tar.gz` with `libbg3-wasd-everywhere.so`,
`bg3-wasd-everywhere.toml`, [INSTALL.md](INSTALL.md) and the license, plus a `.sha256` file. It
needs Docker.

- **Where it builds:** inside the Steam Runtime 3.0 (sniper) SDK image, the runtime the game runs in.
  The library then needs only glibc 2.31, which the runtime ships.
- **What it checks:** the library exports only `SDL_PollEvent` and `SDL_GetWindowGrab`, and it loads
  under the runtime's glibc. The load test starts a stand-in process named `bg3`: the library reads
  its config, finds no game code and disables itself.
- **Options:** `SDK_IMAGE` sets the image and `SDK_CXX` sets the compiler inside it.

## Run

In Steam, open Baldur's Gate 3 → Properties → General → Launch Options and enter:

```
LD_PRELOAD="/absolute/path/to/libbg3-wasd-everywhere.so:$LD_PRELOAD" %command%
```

The path must not contain spaces. Keeping `$LD_PRELOAD` keeps the Steam overlay working.

- **Config:** `bg3-wasd-everywhere.toml` next to `libbg3-wasd-everywhere.so`.
- **Log:** `~/.local/state/bg3-wasd-everywhere/bg3-wasd-everywhere.log` (or
  `$XDG_STATE_HOME/bg3-wasd-everywhere/`). Inside the Steam container, the directory of an
  `LD_PRELOAD` library is read-only, so the log cannot go next to it.
- **Rebuilding:** don't rebuild while the game is running.

## How it works

- The library's constructor runs in every process that inherits `LD_PRELOAD`, and does nothing
  unless the process is `bg3`.
- The addresses are found by signature in the game's executable segment
  (`src/signatures.cpp`, mapped from the Windows signatures).
- The four `call GetInputValue` in `GetMovementInputWorldVec` are redirected through a jump stub
  allocated within ±2 GiB of the game code. On Linux, `GetInputValue` takes the command id in `esi`
  and returns the value in `xmm0`.
- `ExecuteCameraCommand` is only called through a vtable, so its single vtable slot is pointed at
  the hook. On Linux, it takes `Command*` in `rdx`, and the hook sets the command id to 0 for
  CameraBackward..CameraRight (99–102).
- Camera rotation starts when the game pushes the rotation cursor (id 0x2E) onto its cursor
  manager and stops when it pops it. On Windows the mod calls these CameraRotationOn/Off. Both functions
  are hooked at their entry. The virtual call to `SetVirtualCursorPos` in the SDL event loop is replaced, so
  the game's virtual cursor can be moved off-screen while rotating. SDL relative mouse mode is
  switched once per frame, at the start of the game's `SDL_PollEvent` loop.
- Hotkeys come from the game's own SDL event stream. The library defines `SDL_PollEvent`, which the
  game imports from `libSDL2.so`, forwards each call to the real function, and inspects the events.
