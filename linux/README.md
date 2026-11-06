# BG3WASD for native Linux

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
- Hotkeys and settings from `BG3WASD.toml`: `ToggleMovementMode`, `HoldMovementMode`,
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
- In Character mode outside combat, moving the cursor to the screen edge doesn't pan the camera, and
  the chase camera keeps the character (`BlockEdgePanningHook` and
  `PreventLosingChaseCamTargetHook`). On Linux, `GetViewport` is inlined. The mod rewrites the `jne`
  that skips edge panning with a gamepad into `nop ; jmp`, a two-byte change made with one store,
  and switches it back when the mode or combat changes.
- `BG3WASD.toml` is reloaded about a second after it changes.
- Movement keys are bound automatically, like `InputconfigPatcher` on Windows.
  - What it does: binds `CharacterMoveForward/Backward/Left/Right` to the keys of
    `CameraForward/Backward/Left/Right` (or the defaults), drops extra `key:unknown` entries, and, when
    right click rotates the camera, binds its modifier variants and moves Ping off right click.
  - Where: `inputconfig_p1.json` of every player profile in
    `~/.local/share/Larian Studios/Baldur's Gate 3/PlayerProfiles/`.
  - When: once at startup, before the game reads the file. A file that's already right is not
    rewritten. Before the first change, the original is saved as `inputconfig_p1.json.bg3wasd.bak`.
  - Rebinding: the Windows mod re-patches after keybinding changes in the game's menu; here, restart
    the game.
  - Check offline: `./build/selftest --inputconfig <file> <output>`.

## Not ported yet

- From the camera rotation features:
  - `ToggleCameraRotation` (mouselook without holding a button);
  - automatically turning rotation off in menus, dialogs, the loot window and photo mode;
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

- `build/libbg3wasd.so`: the mod.
- `build/selftest`: checks the signatures against a game binary without starting the game.
- `build/BG3WASD.toml`: the default config. It is copied once, so later builds keep your edits.

To check a game build:

```bash
./build/selftest ~/.local/share/Steam/steamapps/common/"Baldurs Gate 3"/bin/bg3
```

## Run

In Steam, open Baldur's Gate 3 → Properties → General → Launch Options and enter:

```
LD_PRELOAD="/absolute/path/to/linux/build/libbg3wasd.so:$LD_PRELOAD" %command%
```

The path must not contain spaces. Keeping `$LD_PRELOAD` keeps the Steam overlay working.

- **Config:** `BG3WASD.toml` next to `libbg3wasd.so`.
- **Log:** `~/.local/state/BG3WASD/BG3WASD.log` (or `$XDG_STATE_HOME/BG3WASD/`). Inside the Steam
  container, the directory of an `LD_PRELOAD` library is read-only, so the log cannot go next to it.
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
  manager and stops when it pops it. On Windows the mod calls these CameraRotationOn/Off. Both calls
  are redirected. The virtual call to `SetVirtualCursorPos` in the SDL event loop is replaced, so
  the game's virtual cursor can be moved off-screen while rotating. SDL relative mouse mode is
  switched once per frame, at the start of the game's `SDL_PollEvent` loop.
- Hotkeys come from the game's own SDL event stream. The library defines `SDL_PollEvent`, which the
  game imports from `libSDL2.so`, forwards each call to the real function, and inspects the events.
