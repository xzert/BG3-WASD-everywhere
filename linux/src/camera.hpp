#pragma once

#include <cstdint>
#include <string>

#include "memory.hpp"

// Camera rotation with a hidden ("invisible") or centered cursor, ported from CameraRotationOnHook,
// CameraRotationOffHook, PollEventHook and SetVirtualCursorPosHook.
namespace wasd::camera {

enum class RotationCursor { Invisible, Centered, Vanilla };

// "invisible", "centered" or "vanilla"; anything else falls back to invisible.
RotationCursor ParseRotationCursor(const std::string& name);

void SetRotationCursors(RotationCursor character_mode, RotationCursor camera_mode);

// Hooks rotation start/stop and the virtual cursor. Returns false (and changes nothing in the game)
// if the addresses can't be found.
bool Install(const mem::MainExecutable& exe, uintptr_t execute_camera_command);

// Called from the SDL_PollEvent interposer once per frame, before the game reads its events.
void OnFrameStart();

// Every key or mouse button press and release the game receives (main thread). Releasing the input
// that started rotation ends the cursor override even if the game never pops its rotation cursor,
// e.g. when a menu opened mid-rotation and took the release.
void OnInputEvent(bool mouse_button, int code, bool down);

void SetWindow(void* window);

// Whether the camera is being rotated right now (any thread).
bool IsRotating();

// Mouselook (ToggleCameraRotation): the camera keeps rotating without holding the rotate input.
void ToggleMouselook();
void SetMouselook(bool on);
// The input mouselook holds down for the player: CameraToggleMouseRotate's binding.
void SetRotateInput(bool mouse_button, int code);
// Mouselook pauses while a dialog runs.
void SetDialogActive(bool active);

// A panel key (MouselookPauseKeys) was pressed: it opens or closes that panel, and mouselook pauses
// while any panel opened this way is open. Esc closes them all; the mouselook key resumes too.
void OnPanelKey(int key);
void OnCloseAllPanels();

// In Character mode outside combat: no edge panning, and the chase camera keeps its target
// (BlockEdgePanningHook and PreventLosingChaseCamTargetHook on Windows).
bool InstallCharacterModeCamera(const mem::MainExecutable& exe, uintptr_t is_in_controller_mode);

// BlockInteractMove: in Character mode outside combat, with the setting on, a click on the ground
// (Interact) doesn't move the character, neither clicked nor held. Objects and people still work.
bool InstallInteractMoveBlocker(const mem::MainExecutable& exe);

// Call when the movement mode, the combat state or the config changes. Applies the edge panning and
// chase camera patch and the interact move blocker.
void UpdateCharacterModeCamera();

}  // namespace wasd::camera
