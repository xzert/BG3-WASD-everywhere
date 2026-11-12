#pragma once

#include <string>

namespace wasd {

// Camera tweaks for one camera kind, the exploration camera or the combat camera. The names and
// defaults follow Native Camera Tweaks by ersh1, except where noted.
struct CameraModeTweaks {
    bool unlock_pitch = true;
    bool keep_tactical_pitch_locked = false;
    // Native Camera Tweaks allows -85 and keeps the camera above the floor; that floor check isn't
    // ported, so a lower limit can take the camera under the ground.
    float unlocked_pitch_min = 0.0f;
    float unlocked_pitch_max = 85.0f;

    bool override_locked_pitch = false;
    float locked_pitch_close = 0.0f, locked_pitch_far = 0.0f;
    float locked_tactical_pitch_close = 85.55f, locked_tactical_pitch_far = 85.55f;
    float locked_alt_pitch_close = 32.69f, locked_alt_pitch_far = 39.7f;

    bool override_zoom = true;
    float zoom_min = 0.5f, zoom_max = 20.0f;
    float tactical_zoom_min = 10.0f, tactical_zoom_max = 50.0f;
    float alt_zoom_min = 10.0f, alt_zoom_max = 40.0f;

    bool override_fov = false;
    float fov_close = 55.0f, fov_far = 55.0f, tactical_fov = 25.0f;
    float alt_fov_close = 45.0f, alt_fov_far = 45.0f;

    bool override_offset = false;
    float horizontal_offset_mult = 0.0f, vertical_offset_mult = 0.8f;
};

struct CameraTweakSettings {
    bool enabled = true;
    float unlocked_pitch_initial_value = 25.0f;
    float unlocked_pitch_clamp_speed = 5.0f;
    CameraModeTweaks exploration{ .locked_pitch_close = 19.05f, .locked_pitch_far = 40.71f };
    CameraModeTweaks combat{ .locked_pitch_close = 32.73f, .locked_pitch_far = 52.42f };
    float mouse_camera_rotation_mult = 1.0f;
    float mouse_pitch_mult = 0.25f;
    float mouse_zoom_mult = 0.5f;
    bool invert_mouse_pitch = false;
    float keyboard_camera_rotation_mult = 2.0f;
};

// The settings in bg3-wasd-everywhere.toml. Defaults match the Windows mod.
struct Settings {
    std::string toggle_movement_mode = "key:capslock";
    std::string hold_movement_mode;
    std::string toggle_autoforward = "shift+key:w";
    std::string toggle_walkspeed;
    std::string hold_walkspeed;
    std::string reload_config;
    std::string toggle_camerarotation;
    bool toggling_movement_mode_also_toggles_camera_rotation = false;
    // Linux only: keys that open the game's panels (defaults from the game's Adventure Panels keybinds).
    std::string mouselook_pause_keys = "key:i,key:m,key:j,key:k,key:n,key:tab,key:b,key:l,key:p,key:h";
    float walk_speed = 0.3f;
    bool is_walking_default = false;
    bool is_character_movement_default = true;
    std::string rotation_cursor_in_character_mode = "invisible";
    std::string rotation_cursor_in_camera_mode = "invisible";
    bool auto_toggle_movement_mode = true;
    bool walk_after_combat = false;
    bool rotate_plus_lmb_is_forward = true;
    // macOS only so far.
    bool prevent_clicking_active_character = true;
    bool prevent_clicking_camera_occluders = false;
    CameraTweakSettings camera_tweaks;
};

// Reads `path`; keys that are missing keep their defaults. Returns false if the file can't be read.
bool ParseSettingsFile(const std::string& path, Settings& settings);

void SetSettingsPath(std::string path);

// Loads bg3-wasd-everywhere.toml and applies hotkeys and walk speed. The first load also sets the initial
// movement mode and walking state.
void ReloadSettings();

// Watches bg3-wasd-everywhere.toml in a background thread. A change requests a reload, which the main thread
// performs (TakeReloadRequest), so bindings never change under the SDL event handler.
void WatchSettingsFile();
bool TakeReloadRequest();

}  // namespace wasd
