#include "settings.hpp"

#include <cstdlib>
#include <fstream>

#include "camera.hpp"
#include "input.hpp"
#include "log.hpp"
#include "state.hpp"

namespace wasd {
namespace {

std::string g_path;
bool g_loaded_once = false;

std::string Trim(const std::string& s)
{
    const auto begin = s.find_first_not_of(" \t\r");
    if (begin == std::string::npos) {
        return "";
    }
    const auto end = s.find_last_not_of(" \t\r");
    return s.substr(begin, end - begin + 1);
}

// The value of a `key = value` line. Handles 'literal' and "basic" TOML strings; anything after
// the value (a trailing comment) is ignored.
std::string ParseValue(const std::string& raw)
{
    if (!raw.empty() && raw[0] == '\'') {
        const auto end = raw.find('\'', 1);
        return raw.substr(1, end == std::string::npos ? std::string::npos : end - 1);
    }
    if (!raw.empty() && raw[0] == '"') {
        std::string value;
        for (size_t i = 1; i < raw.size() && raw[i] != '"'; ++i) {
            if (raw[i] == '\\' && i + 1 < raw.size()) {
                ++i;
            }
            value += raw[i];
        }
        return value;
    }
    return Trim(raw.substr(0, raw.find('#')));
}

bool ParseBool(const std::string& value, bool fallback)
{
    if (value == "true") {
        return true;
    }
    if (value == "false") {
        return false;
    }
    return fallback;
}

}  // namespace

bool ParseSettingsFile(const std::string& path, Settings& s)
{
    std::ifstream file(path);
    if (!file) {
        return false;
    }
    std::string line;
    while (std::getline(file, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#' || line[0] == '[') {
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = Trim(line.substr(0, eq));
        const std::string value = ParseValue(Trim(line.substr(eq + 1)));

        if (key == "ToggleMovementMode") {
            s.toggle_movement_mode = value;
        } else if (key == "HoldMovementMode") {
            s.hold_movement_mode = value;
        } else if (key == "ToggleAutoforward") {
            s.toggle_autoforward = value;
        } else if (key == "ToggleWalkspeed") {
            s.toggle_walkspeed = value;
        } else if (key == "HoldWalkspeed") {
            s.hold_walkspeed = value;
        } else if (key == "ReloadConfig") {
            s.reload_config = value;
        } else if (key == "WalkSpeed") {
            char* end = nullptr;
            const float speed = std::strtof(value.c_str(), &end);
            if (end != value.c_str()) {
                s.walk_speed = speed;
            }
        } else if (key == "IsWalkingDefault") {
            s.is_walking_default = ParseBool(value, s.is_walking_default);
        } else if (key == "IsCharacterMovementDefault") {
            s.is_character_movement_default = ParseBool(value, s.is_character_movement_default);
        } else if (key == "CharacterMovementRotationCursor") {
            s.rotation_cursor_in_character_mode = value;
        } else if (key == "CameraMovementRotationCursor") {
            s.rotation_cursor_in_camera_mode = value;
        }
    }
    return true;
}

void SetSettingsPath(std::string path) { g_path = std::move(path); }

void ReloadSettings()
{
    Settings settings;
    if (ParseSettingsFile(g_path, settings)) {
        LOG_INFO("Config loaded from %s", g_path.c_str());
    } else {
        LOG_WARN("Could not read %s, using defaults", g_path.c_str());
    }
    if (settings.walk_speed < 0.0f || settings.walk_speed > 1.0f) {
        LOG_WARN("WalkSpeed %.2f is outside [0, 1], clamping", settings.walk_speed);
        settings.walk_speed = settings.walk_speed < 0.0f ? 0.0f : 1.0f;
    }

    State& state = GetState();
    state.walk_speed = settings.walk_speed;
    input::SetBindings(settings);
    camera::SetRotationCursors(camera::ParseRotationCursor(settings.rotation_cursor_in_character_mode),
        camera::ParseRotationCursor(settings.rotation_cursor_in_camera_mode));

    if (!g_loaded_once) {
        // Set directly: unlike a hotkey toggle, startup should not nudge the character forward.
        state.movement_mode_toggled = settings.is_character_movement_default;
        state.walking_toggled = settings.is_walking_default;
        g_loaded_once = true;
    }
}

}  // namespace wasd
