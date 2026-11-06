#pragma once

#include <string>

namespace wasd {

// The part of BG3WASD.toml the Linux prototype understands. Defaults match the Windows mod.
struct Settings {
    std::string toggle_movement_mode = "key:capslock";
    std::string hold_movement_mode;
    std::string toggle_autoforward = "shift+key:w";
    std::string toggle_walkspeed;
    std::string hold_walkspeed;
    std::string reload_config;
    float walk_speed = 0.3f;
    bool is_walking_default = false;
    bool is_character_movement_default = true;
    std::string rotation_cursor_in_character_mode = "invisible";
    std::string rotation_cursor_in_camera_mode = "invisible";
    bool auto_toggle_movement_mode = true;
    bool walk_after_combat = false;
    bool rotate_plus_lmb_is_forward = true;
};

// Reads `path`; keys that are missing keep their defaults. Returns false if the file can't be read.
bool ParseSettingsFile(const std::string& path, Settings& settings);

void SetSettingsPath(std::string path);

// Loads BG3WASD.toml and applies hotkeys and walk speed. The first load also sets the initial
// movement mode and walking state.
void ReloadSettings();

// Watches BG3WASD.toml in a background thread. A change requests a reload, which the main thread
// performs (TakeReloadRequest), so bindings never change under the SDL event handler.
void WatchSettingsFile();
bool TakeReloadRequest();

}  // namespace wasd
