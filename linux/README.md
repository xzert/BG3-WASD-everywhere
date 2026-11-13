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
- `bg3-wasd-everywhere.toml` is reloaded one to two seconds after it changes. The game's first `bg3`
  process forks the one that plays, so the mod has no threads of its own: the main thread checks the
  file's inotify watch once a second.
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
- `PreventClickingActiveCharacter`: in Character mode the cursor ignores the active character, as on
  Windows. The game's picking skips objects that `ls::IsFading` reports as fading out; the mod
  redirects the seven calls of it from the picking code and answers "fading" for the active
  character. Character highlights and sound occlusion call it too and keep the game's answer. The
  active character comes from `gui::GameUI::OnSelectedCharacterChanged`. Both names come from the
  macOS build, which keeps them.
  - `MaxClickDistanceBehindCharacter` (1 m by default) limits it: when the cursor's ray reaches the
    ground further behind the character, the character stays clickable, as in vanilla. Otherwise a
    low camera sent the character running off to a point far behind them. The ray comes from the
    camera occluders' stub, which sees `GameObjectPick`'s ray just before that function asks
    `IsFading`; the ground is at the height of the feet, from the camera's fields.
- `PreventClickingCameraOccluders` (off by default, as on Windows): in Character mode the picking
  ray of `ecl::PickingHelper::GameObjectPick` starts the camera's zoom distance further along, so
  what stands between the camera and the character can't be clicked. A steep or distant camera
  (zoom × pitch ≥ 300) is an exception, as on Windows. The mod replaces the ray's call of
  `phx::PhysXScene::RaycastAll` with a stub that moves its start; the camera
  (`ecl::GameCameraBehavior`) comes from `GetCameraPitchDegrees`, which the camera update calls
  every frame.
  - Walk-back protection, with the same setting and exception: the ray of
    `ecl::PickingHelper::WalkablePick`, which finds the ground a click moves to, starts at the
    vertical plane through the character (`MaxClickDistanceTowardCamera`, 0.6 m by default, on the
    camera's side of it). Ground, stairs and ledges between the camera and the character then don't
    send the character back toward the camera. A plane rather than the zoom distance, because below
    a close camera the ground is further away than the zoom. When the ray is already below the
    character's feet at that plane, the click is on the ground between them and is cancelled:
    started there, the ray would begin underground, and the game could pick a spot ahead of the
    character. The mod replaces the ray's `mov dword [rsp], 0x194280 ; call rax` with a stub.
- A movement key stops a click move in Character mode outside combat, like
  `HandleCharacterCommandCavehook` on Windows. When `ActionCancel` (input 174, the right button) is
  released, `ecl::InputController::OnInputEvent` ends the running task or asks the server to stop
  the character. The mod hooks that function at its entry and, when the first movement key goes
  down, passes it such a release before the key's own event. With another movement key already
  held, it sends nothing, so moving by WASD doesn't stutter.
- `BlockInteractMove` (off by default, as on Windows): in Character mode outside combat, a click on
  the ground doesn't move the character, neither clicked nor held; objects and people still work.
  The function names come from the macOS build. In `ecl::CharacterTask_MoveTo::OnInputEvent`, the
  `je` after the Interact check (`cmp dword [rsi], 4`) becomes a nop, so a click starts no move. In
  `ecl::InputController::OnInputEvent`, the constant that sets the "held" flag (+0x21) becomes 0, so
  holding the button starts none either. Both are two-byte writes, switched with the movement mode,
  combat and the config. Unlike Windows, the mod lets click moves through in combat, even in
  Character mode, so turn-based fights stay playable.
- Camera tweaks (`CameraTweaks`, on by default), ported from
  [Native Camera Tweaks](https://github.com/ersh1/BG3_NativeCameraTweaks) by ersh1. The config keys
  are the same as in `BG3NativeCameraTweaks.toml`.
  - A free camera pitch: move the mouse up and down while you rotate the camera, mouselook included.
    The camera update asks `GetCameraPitchDegrees` for the pitch the camera turns to; for that one
    call (found by its return address) the mod answers with its own pitch, and sets the game's pitch
    speed so the camera follows at once.
  - The game's camera settings (`eoc::CameraSettings`): zoom limits (0.5 to 20 by default), the locked
    pitch, FOV and offsets, for the exploration and the combat camera. They are found through the
    same code; on Linux their offsets are 0x28 higher than on Windows.
  - Zoom and keyboard rotation speed: inside `ExecuteCameraCommand`, the two instructions that write
    the speed into the camera call the mod instead, which multiplies the speed and writes it. The game
    picks the camera, so the speed lands in the one the command is for. The mouse rotation input is
    multiplied in the `ExecuteCameraCommand` hook.
  - Not ported: the gamepad settings, keeping the camera above the floor
    (`UnlockedPitchLimitClipping`) and keeping the zoom on a zone change (`ResetZoomOnZoneChange`).
    Without the floor check, the pitch limit defaults to 0 rather than -85.

## Not ported yet

- From the camera rotation features:
  - mouselook doesn't pause by itself for photo mode; if you open it with a key, add the key to
    `MouselookPauseKeys`;
  - the crosshair icon for `centered`.
- Hotkeys still work while menus are open. They are ignored only while you type into a text field.
- From the camera tweaks: the gamepad settings, the floor check and keeping the zoom on a zone
  change.

## Known issues

- A click on the active character above eye level still sends them far ahead, as in vanilla: the
  character's shape for picking ends around the eyes, so the game takes the click for the ground
  behind them, and `MaxClickDistanceBehindCharacter` doesn't apply.
- With the game's Edge Panning option on, moving the cursor to the screen edge doesn't pan the camera
  in Camera mode either. The game itself doesn't allow it there: in that mode the mod has restored
  the game's original code (the log shows `Edge panning allowed (code now 0f 85)`), and on macOS,
  where the mod asks the game's `IsEdgePanningAllowed` and passes the answer on, the game says no.
  Panning with the movement keys works.
- Mouselook interrupted by an NPC's dialog (not properly tested). While mouselook is on, the
  cursor picks nothing, so you turn it off before you click a chest or a trader yourself. A dialog
  that an NPC starts pauses it, and mouselook comes back when the dialog ends, even if the dialog
  opened a trade or loot window. Press the mouselook key to turn it off.

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
  `LD_PRELOAD` library is read-only, so the log cannot go next to it. It has the startup, the
  settings and mode changes. With `DebugLog = true` in the config, it also gets each decision of the
  mod (clicks, camera rotation, the cursor, mouselook pauses), for bug reports.
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
