# BG3 WASD Everywhere for macOS

An early port for the native macOS game on Apple Silicon. Supported game version: 4.1.1.7631656.

## What works

- The movement keys move the character, like `WASDUnlockPatch` on Windows and on Linux. The game
  runs its gamepad movement task (`ecl::CharacterTask_MoveController`) only while its
  controller-mode flag is set; the library turns that check into a `nop`.
- Hotkeys and settings come from `bg3-wasd-everywhere.toml` next to the library, as on Linux. The
  config is reloaded about a second after you save it. It has the same keys as the Linux config.
- `ToggleMovementMode` (Caps Lock) and `HoldMovementMode` switch between Character and Camera
  movement mode.
  - In Character mode outside combat, the movement keys don't pan the camera: the camera movement
    actions (ids 99 to 102) reach `ecl::CameraSystem::OnInputEvent` with id 0. Edge panning is off
    too: `ecl::CameraSystem::IsEdgePanningAllowed` says no, as `BlockEdgePanningHook` does on
    Windows. In Camera mode the hook passes the game's own answer on, and the game doesn't allow
    edge panning there either (the log says `The game doesn't allow edge panning`), as on Linux.
  - In Camera mode they only pan it: `GetRotatedInput`, the movement input of the movement task, is
    hooked and returns no movement. A gamepad keeps working as without the mod.
- `ToggleAutoforward` (Shift+W) toggles autoforward in Character mode. The game gets a key-down of
  the Forward key from the mod instead of the hotkey press. Pressing Forward or Backward, the hotkey
  again or switching to Camera mode ends it, as in the Linux port.
- `ToggleWalkspeed` and `HoldWalkspeed` switch to walking: the same hook shortens the movement input
  to `WalkSpeed`, like a gamepad stick pushed part of the way.
- Key names are the same as on Linux: physical key positions (USB HID usage ids), so the defaults
  work on any layout. Option is `alt`, Command is `gui`.
- While a text field of the game has the keyboard focus (a save name, the chat), key presses don't
  trigger hotkeys, as on Linux. The library asks the game itself,
  `gui::GameUI::IsTextInputElementFocused`, with the GameUI object that a stub in the
  `GameUI::Update` vtable slot remembers every frame.
- Camera rotation (`CharacterMovementRotationCursor`, `CameraMovementRotationCursor`): while the
  game shows its rotation cursor (`ecl::CursorControl::AddForcedMouseCursor` with mode 0x2E, the same
  as on Linux), the library takes the cursor over.
  - The cursor is detached from the mouse with `CGAssociateMouseAndMouseCursorPosition`, so it can't
    hit the screen edge, and the game keeps turning the camera by the mouse's deltas.
  - `invisible` hides it where it was, and moves the game's own cursor position (`gCore`) off-screen
    so nothing gets hovered or clicked. `centered` moves it to the window's center. Both bring it
    back when rotation ends, when the rotate input is released or when the game loses focus.
  - It takes over only after the mouse has moved 6 points, so a plain click of the rotate input
    stays a click.
- `RotatePlusLeftclickMovesForward`: while rotating, the left mouse button walks forward. The game
  gets the Forward key from the mod instead of the click.
- Mouselook (`ToggleCameraRotation`, `TogglingMovementModeAlsoTogglesCameraRotation`): as on Linux,
  the mod holds the game's rotate input (`CameraToggleMouseRotate`, the middle mouse button by
  default) for the player, with presses of its own through the original `sendEvent:`.
  - It lets go of the input, and presses it again afterwards, while the game is in the background, a
    text field has the focus, a panel opened with its key is open (`MouselookPauseKeys`) or the game
    stops asking for the movement input: the Esc menu, other menus and loading screens.
  - Dialogs pause it through `ecl::GameAnalyticsSystem::RecordDialogStart` and `RecordDialogEnd`, not
    checked in game yet. It also hooks `gui::GameUI::OnIsGameMenuOpenChanged`, which the Esc menu
    didn't call in the first test. Both log their calls.
  - During its press, the game's cursor is off-screen, so a HUD button under the cursor can't take
    the press. A press that doesn't start rotation is retried for about a second.
  - The game takes every mouse button but the left and the right one as its middle button. So a
    mouse button bound to `ToggleCameraRotation` doesn't reach the game, and while the mod holds the
    rotate input, the player's own presses of it don't either.
- `PreventClickingActiveCharacter`: in Character mode the cursor ignores the active character, as on
  Windows. The game's picking skips objects that `ls::IsFading` reports as fading out; the library
  makes it report so for the active character when `ecl::PickingHelper` asks, and only then. The
  active character comes from `gui::GameUI::OnSelectedCharacterChanged`.
  - `MaxClickDistanceBehindCharacter` (1 m by default) limits it: when the cursor's ray reaches the
    ground further behind the character, the character stays clickable, as in vanilla. Otherwise a
    low camera sent the character running off to a point far behind them. The ray comes from the
    `RaycastAll` hook of the camera occluders, which `GameObjectPick` calls just before it asks
    `IsFading`; the ground is at the height of the feet, from the camera's fields.
- `PreventClickingCameraOccluders` (off by default, as on Windows): in Character mode the picking ray
  of `ecl::PickingHelper::GameObjectPick` starts the camera's zoom distance further along, so what
  stands between the camera and the character can't be clicked. As on Windows, a steep or distant
  camera (zoom × pitch ≥ 300) is an exception. The library replaces `phx::PhysXScene::RaycastAll`
  in its vtable and changes only rays cast from `GameObjectPick`. The camera
  (`ecl::GameCameraBehavior`) is the one the camera tweaks' pitch update sees, the one the player
  looks through; without that hook, it comes from `ecl::CameraSystem::CalcCamTargetDest`.
  - Walk-back protection, with the same setting and exception: rays cast from
    `ecl::PickingHelper::WalkablePick`, which finds the ground a click moves to, start at the
    vertical plane through the character (`MaxClickDistanceTowardCamera`, 0.6 m by default, on the
    camera's side of it), so ground, stairs and ledges between the camera and the character don't
    send the character back toward the camera. A click on the ground between them is cancelled, as
    on Linux.
- Camera tweaks (`CameraTweaks`, on by default), the same as on Linux and with the same core
  (`../linux/src/camera_tweaks.cpp`), ported from
  [Native Camera Tweaks](https://github.com/ersh1/BG3_NativeCameraTweaks) by ersh1: the free pitch
  with the mouse while rotating, the zoom limits, locked pitch, FOV and offsets, and the zoom and
  keyboard rotation speeds. Three 16-byte windows of game code become jumps to stubs in
  `src/stubs.S`, which do the same instructions with the tweak and jump back:
  - in `ecl::CameraSystem::UpdateGameCameraBehavior`, the call of
    `ecl::GameCameraBehavior::GetCameraPitchDegrees` that gives the pitch the camera turns to
    (the function itself starts with branches, so it can't take an inline hook). The
    `eoc::CameraSettings` pointer and the camera definition offsets come from the code just before
    that call;
  - in `ecl::CameraSystem::OnInputEvent`, the two stores of the zoom and rotation speeds into the
    camera.

  Not ported, as on Linux: the gamepad settings, the floor check and keeping the zoom on a zone
  change.
- `AutoToggleMovementMode`: Camera mode when combat or turn-based mode (FTB) starts, Character mode
  when it ends, as on Linux. The game's own analytics events tell when: the library hooks
  `ecl::GameAnalyticsSystem::FillAllEventsData(json, name)`, which starts the data of every event,
  and looks for `combat_start`, `combat_end`, `ftb_start` and `ftb_end`. A save loaded in the
  middle of a fight isn't noticed until the next combat change. `SwitchToWalkingAfterCombat` works
  too.
- A movement key stops a click move in Character mode outside combat, as on Linux: the hook of
  `ecl::InputController::OnInputEvent` passes the game an `ActionCancel` release before the first
  movement key goes down.
- `BlockInteractMove` (off by default): in Character mode outside combat, clicking or holding
  Interact (the left button) on the ground doesn't move the character. As on Linux, combat stays
  vanilla even in Character mode, so turn-based fights stay playable.
  - A click: `ecl::CharacterTask_MoveTo::OnInputEvent` doesn't get Interact.
  - Holding: `ecl::InputController::OnInputEvent` sets its "held" byte (+0x21) for Interact, and the
    hook clears it right after.
  - Windows and Linux patch those two places in the game code whenever the mode changes. On macOS,
    rewriting code takes away its execute permission for a moment, and another game thread could
    run into that, so these inline hooks stay in place and read a flag.
- The chase camera keeps its target in Character mode outside combat, like
  `PreventLosingChaseCamTargetHook` on Windows, a fix of a rare symptom: the test whether a point is
  inside the target's bounds says no. `ls::trigger::HitTest` starts with a branch and many systems
  call it, so the call in `UpdateGameCameraBehavior` is replaced with a 16-byte jump to a stub, as
  for the camera tweaks.
- The movement keys are bound in `inputconfig_p1.json` of every player profile in
  `~/Documents/Larian Studios/Baldur's Gate 3/PlayerProfiles/`. It uses the same patcher as the Linux
  port, and keeps the original as `inputconfig_p1.json.bg3-wasd-everywhere.bak` before changing it.
- The macOS game keeps its function names, so the library finds code by name instead of byte
  signatures, then finds the instructions it patches by their pattern (`src/patterns.cpp`).

The log has the startup, the settings and mode changes. With `DebugLog = true` in the config, it
also gets each decision of the library (clicks, camera rotation, the cursor, mouselook pauses), the
symbol addresses, the raw camera input events and the key and mouse button events it sees.

## Build

Needs the Xcode Command Line Tools (`xcode-select --install`). The `macos/` folder builds files of
the Linux port (`log.cpp`, `inputconfig.cpp`, `settings_file.cpp` and `hotkeys.cpp` in
`../linux/src`), so keep the two folders next to each other.

```bash
make
```

This builds `build/libbg3-wasd-everywhere.dylib` (arm64), `build/bg3-wasd-everywhere-launch` and
`build/symtest`. It also copies the default config to `build/bg3-wasd-everywhere.toml`, only once, so
your edits to it survive rebuilds.

To build the release archive `dist/bg3-wasd-everywhere-macos-<version>.tar.gz`:

```bash
./release.sh
```

It builds in `dist/.build` without debug info, runs `make check`, strips the library and the
launcher, signs them ad hoc and packs them with the default config, `INSTALL.md` and `LICENSE`.

## Check

```bash
./build/symtest ~/Library/"Application Support"/Steam/steamapps/common/"Baldurs Gate 3"/"Baldur's Gate 3.app"/Contents/MacOS/"Baldur's Gate 3"
```

`symtest` finds the game functions the library uses and the instructions it patches, the same way
the library does in memory. It also checks the key code tables and the hotkey and config code. It
should end with `OK`. It also builds on Linux.

To try the library without the game:

```bash
make check
```

This builds `build/standin`, a small program with the same symbols and vtable slot as the game.
It runs as "Baldur's Gate 3" with the library preloaded and a test config next to it.
- It checks that every hooked call still returns the right value.
- It sends the key and mouse events of the test config's hotkeys and checks what the camera, the
  movement task and the game's `sendEvent:` get: movement modes, autoforward and walking.
- It starts and ends camera rotation the way the game does, and checks the game's cursor position
  and rotate + left click. Over SSH the real cursor is left alone.
- It plays the game for mouselook: the middle button shows the rotation cursor, and the movement
  input is asked for every 50 ms. It checks each pause: text field, panel key, game menu, dialog
  and no movement input.
- It changes the config and checks that `ReloadConfig` and saving the file both load it.

`HOME` points at a scratch folder, so the real log, config and key bindings are not touched.

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

- `src/main.mm`: the library: the WASD unlock, the hooks, the hotkeys, the config and the probes.
- `src/keycodes.cpp`: macOS virtual key codes and modifier flags to the HID usage ids of the key
  names.
- `bg3-wasd-everywhere.toml`: the default config.
- `../linux/src/hotkeys.cpp`, `settings_file.cpp`, `camera_tweaks.cpp`: the key names, hotkey
  rules, config parser and camera tweaks, shared with the Linux port.
- `src/launch.cpp`: the launcher for Steam's launch options.
- `src/patterns.cpp`: finds the instructions to patch inside the game functions.
- `src/stubs.S`: pass-through stubs, which keep every argument register so a hook works whatever the
  function's signature is, and the camera tweak stubs.
- `src/macho.cpp`: the symbol table reader, for a loaded image or a file.
- `src/arm64.cpp`: instruction decoding for patterns and inline hooks.
- `src/symbols.hpp`: the game symbols the library uses.
- `tests/symtest.cpp`: runs the same lookups on the game's executable.
- `tests/standin.mm`, `tests/run-standin.sh`: `make check`.
