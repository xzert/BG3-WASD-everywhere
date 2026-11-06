#pragma once

#include <atomic>

namespace wasd {

// Mod state shared by the SDL event thread (hotkeys) and the game thread (input hooks).
struct State {
    std::atomic<bool> movement_mode_toggled{ true };  // true means Character movement mode
    std::atomic<bool> movement_mode_held{ false };
    std::atomic<bool> autoforward{ false };
    std::atomic<bool> walking_toggled{ false };
    std::atomic<bool> walking_held{ false };
    std::atomic<float> walk_speed{ 0.3f };

    // Frames left to fake a forward input, which pulls the camera back to the character.
    std::atomic<int> frames_to_center_camera{ 0 };

    // The game's own flag; null until the signatures are resolved.
    const volatile bool* is_in_controller_mode = nullptr;

    bool IsCharacterMovementMode() const { return movement_mode_toggled != movement_mode_held; }
    bool IsInControllerMode() const { return is_in_controller_mode && *is_in_controller_mode; }

    void SetMovementModeToggled(bool value);
    void SetMovementModeHeld(bool value);
};

State& GetState();

}  // namespace wasd
