#pragma once

#include <cstdint>

#include "settings.hpp"

// Camera tweaks ported from Native Camera Tweaks by ersh1 (GPL-3.0-or-later): a free camera pitch
// controlled with the mouse, wider zoom limits, and the game's camera settings (locked pitch, FOV,
// offsets) from the config. Shared by the Linux and macOS ports; each port finds the game's data
// and calls the hooks below.
namespace wasd::camera_tweaks {

// Where the game keeps its camera settings, eoc::CameraSettings: a global pointer to an object with
// one camera definition per camera kind. The offsets differ between the Linux, macOS and Windows
// builds; the fields inside a definition are the same.
struct GameLayout {
    uint8_t* const* settings = nullptr;  // the global pointer
    uint32_t override_flag = 0;          // a bool: if set, the override definition is used
    uint32_t exploration = 0;
    uint32_t combat = 0;
    uint32_t override_definition = 0;
};

void SetGameLayout(const GameLayout& layout);

// New settings from the config (any thread). They take effect on the next camera update.
void Configure(const CameraTweakSettings& settings);

// Vertical mouse movement in pixels (positive is down), from the platform's input events.
void OnMouseMotion(int dy);

// The camera update's call of GetCameraPitchDegrees, the pitch the camera turns to (game thread).
// `camera` is the ecl::GameCameraBehavior. `rotating` is true while the player rotates the camera
// with the mouse. Returns the pitch to use instead of `game_pitch`.
float OnUpdatePitch(uint8_t* camera, float game_pitch, bool rotating, bool controller);

// Before a camera command (ExecuteCameraCommand on Linux, CameraSystem::OnInputEvent on macOS):
// scales the mouse rotation input in the command, and returns the multipliers for the zoom and
// keyboard rotation speeds the command may set. The port applies those where the game writes the
// speed into the camera, because only the game knows which camera the command is for.
struct CommandSpeed {
    float zoom = 1.0f;
    float keyboard_rotation = 1.0f;
};
CommandSpeed BeforeCameraCommand(uint8_t* command, bool controller);

}  // namespace wasd::camera_tweaks
