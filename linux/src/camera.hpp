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

}  // namespace wasd::camera
