#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

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

    // Settings read by the game thread.
    std::atomic<bool> auto_toggle_movement_mode{ true };
    std::atomic<bool> walk_after_combat{ false };
    std::atomic<bool> rotate_plus_lmb_is_forward{ true };
    std::atomic<bool> rotate_keys_include_lmb{ false };  // from the game's key bindings
    std::atomic<bool> mode_toggles_mouselook{ false };   // TogglingMovementModeAlsoTogglesCameraRotation
    std::atomic<bool> prevent_clicking_active_character{ true };
    std::atomic<float> max_click_distance_behind_character{ 1.0f };  // meters
    std::atomic<float> max_click_distance_toward_camera{ 0.6f };     // meters, walk-back protection
    std::atomic<uint64_t> active_character{ 0 };  // its entity handle, from OnSelectedCharacterChanged
    std::atomic<bool> prevent_clicking_camera_occluders{ false };
    std::atomic<bool> block_interact_move{ false };
    std::atomic<const uint8_t*> camera{ nullptr };  // ecl::GameCameraBehavior, from the camera update

    // When the game last asked for the movement input (it does so every frame while the player can move).
    std::atomic<int64_t> last_movement_poll_ms{ 0 };

    std::atomic<bool> in_combat{ false };
    std::atomic<int64_t> last_combat_change_ms{ 0 };
    std::atomic<bool> mouse_left_down{ false };
    std::atomic<bool> rotate_and_lmb_last_frame{ false };

    // The game's own flag; null until the signatures are resolved.
    const volatile bool* is_in_controller_mode = nullptr;

    bool IsCharacterMovementMode() const { return movement_mode_toggled != movement_mode_held; }
    bool IsInControllerMode() const { return is_in_controller_mode && *is_in_controller_mode; }

    void SetMovementModeToggled(bool value);
    void SetMovementModeHeld(bool value);
};

State& GetState();

inline int64_t NowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

}  // namespace wasd
