#include "settings.hpp"

#include <cstring>
#include <fstream>

// Reading bg3-wasd-everywhere.toml, without the platform: shared by the Linux and macOS ports.
namespace wasd {
namespace {

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

// A decimal number such as 0.3, read the same way in every locale. strtof follows the C locale,
// which the game may set to one with a decimal comma; it would then read "0.3" as 0.
bool ParseNumber(const std::string& text, float& number)
{
    size_t i = 0;
    const bool negative = !text.empty() && text[0] == '-';
    if (negative || (!text.empty() && text[0] == '+')) {
        ++i;
    }
    double digits = 0.0, divisor = 1.0;
    bool any = false, point = false;
    for (; i < text.size(); ++i) {
        const char c = text[i];
        if (c >= '0' && c <= '9') {
            digits = digits * 10.0 + (c - '0');
            divisor *= point ? 10.0 : 1.0;
            any = true;
        } else if (c == '.' && !point) {
            point = true;
        } else {
            break;
        }
    }
    if (any) {
        number = static_cast<float>((negative ? -digits : digits) / divisor);
    }
    return any;
}

// Keys of one camera kind, e.g. "ExplorationZoomMin"; `prefix` is "Exploration" or "Combat".
template <typename Add>
void AddModeKeys(const char* prefix, CameraModeTweaks& m, Add add)
{
    add(prefix, "UnlockPitch", nullptr, &m.unlock_pitch);
    add(prefix, "KeepTacticalPitchLocked", nullptr, &m.keep_tactical_pitch_locked);
    add(prefix, "UnlockedPitchMin", &m.unlocked_pitch_min, nullptr);
    add(prefix, "UnlockedPitchMax", &m.unlocked_pitch_max, nullptr);
    add(prefix, "OverrideLockedPitch", nullptr, &m.override_locked_pitch);
    add(prefix, "LockedPitchClose", &m.locked_pitch_close, nullptr);
    add(prefix, "LockedPitchFar", &m.locked_pitch_far, nullptr);
    add(prefix, "LockedTacticalPitchClose", &m.locked_tactical_pitch_close, nullptr);
    add(prefix, "LockedTacticalPitchFar", &m.locked_tactical_pitch_far, nullptr);
    add(prefix, "LockedAltPitchClose", &m.locked_alt_pitch_close, nullptr);
    add(prefix, "LockedAltPitchFar", &m.locked_alt_pitch_far, nullptr);
    add(prefix, "OverrideZoom", nullptr, &m.override_zoom);
    add(prefix, "ZoomMin", &m.zoom_min, nullptr);
    add(prefix, "ZoomMax", &m.zoom_max, nullptr);
    add(prefix, "TacticalZoomMin", &m.tactical_zoom_min, nullptr);
    add(prefix, "TacticalZoomMax", &m.tactical_zoom_max, nullptr);
    add(prefix, "AltZoomMin", &m.alt_zoom_min, nullptr);
    add(prefix, "AltZoomMax", &m.alt_zoom_max, nullptr);
    add(prefix, "OverrideFOV", nullptr, &m.override_fov);
    add(prefix, "FOVClose", &m.fov_close, nullptr);
    add(prefix, "FOVFar", &m.fov_far, nullptr);
    add(prefix, "TacticalFOV", &m.tactical_fov, nullptr);
    add(prefix, "AltFOVClose", &m.alt_fov_close, nullptr);
    add(prefix, "AltFOVFar", &m.alt_fov_far, nullptr);
    add(prefix, "OverrideOffset", nullptr, &m.override_offset);
    add(prefix, "HorizontalOffsetMult", &m.horizontal_offset_mult, nullptr);
    add(prefix, "VerticalOffsetMult", &m.vertical_offset_mult, nullptr);
}

// Sets a camera tweak; false if `key` isn't one. The keys are those of Native Camera Tweaks.
bool ParseCameraTweak(const std::string& key, const std::string& value, CameraTweakSettings& t)
{
    bool found = false;
    auto add = [&](const char* prefix, const char* name, float* number, bool* flag) {
        const size_t n = std::strlen(prefix);
        if (found || key.compare(0, n, prefix) != 0 || key.compare(n, std::string::npos, name) != 0) {
            return;
        }
        found = true;
        if (number) {
            ParseNumber(value, *number);
        } else {
            *flag = ParseBool(value, *flag);
        }
    };
    add("", "CameraTweaks", nullptr, &t.enabled);
    add("", "UnlockedPitchInitialValue", &t.unlocked_pitch_initial_value, nullptr);
    add("", "UnlockedPitchClampSpeed", &t.unlocked_pitch_clamp_speed, nullptr);
    add("", "MouseCameraRotationMult", &t.mouse_camera_rotation_mult, nullptr);
    add("", "MousePitchMult", &t.mouse_pitch_mult, nullptr);
    add("", "MouseZoomMult", &t.mouse_zoom_mult, nullptr);
    add("", "InvertMousePitch", nullptr, &t.invert_mouse_pitch);
    add("", "KeyboardCameraRotationMult", &t.keyboard_camera_rotation_mult, nullptr);
    AddModeKeys("Exploration", t.exploration, add);
    AddModeKeys("Combat", t.combat, add);
    return found;
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
        } else if (key == "ToggleCameraRotation") {
            s.toggle_camerarotation = value;
        } else if (key == "MouselookPauseKeys") {
            s.mouselook_pause_keys = value;
        } else if (key == "TogglingMovementModeAlsoTogglesCameraRotation") {
            s.toggling_movement_mode_also_toggles_camera_rotation =
                ParseBool(value, s.toggling_movement_mode_also_toggles_camera_rotation);
        } else if (key == "WalkSpeed") {
            ParseNumber(value, s.walk_speed);
        } else if (key == "IsWalkingDefault") {
            s.is_walking_default = ParseBool(value, s.is_walking_default);
        } else if (key == "IsCharacterMovementDefault") {
            s.is_character_movement_default = ParseBool(value, s.is_character_movement_default);
        } else if (key == "CharacterMovementRotationCursor") {
            s.rotation_cursor_in_character_mode = value;
        } else if (key == "CameraMovementRotationCursor") {
            s.rotation_cursor_in_camera_mode = value;
        } else if (key == "AutoToggleMovementMode") {
            s.auto_toggle_movement_mode = ParseBool(value, s.auto_toggle_movement_mode);
        } else if (key == "SwitchToWalkingAfterCombat") {
            s.walk_after_combat = ParseBool(value, s.walk_after_combat);
        } else if (key == "PreventClickingCameraOccluders") {
            s.prevent_clicking_camera_occluders = ParseBool(value, s.prevent_clicking_camera_occluders);
        } else if (key == "PreventClickingActiveCharacter") {
            s.prevent_clicking_active_character = ParseBool(value, s.prevent_clicking_active_character);
        } else if (key == "MaxClickDistanceBehindCharacter") {
            ParseNumber(value, s.max_click_distance_behind_character);
        } else if (key == "BlockInteractMove") {
            s.block_interact_move = ParseBool(value, s.block_interact_move);
        } else if (key == "DebugLog") {
            s.debug_log = ParseBool(value, s.debug_log);
        } else if (key == "MaxClickDistanceTowardCamera") {
            ParseNumber(value, s.max_click_distance_toward_camera);
        } else if (key == "RotatePlusLeftclickMovesForward") {
            s.rotate_plus_lmb_is_forward = ParseBool(value, s.rotate_plus_lmb_is_forward);
        } else {
            ParseCameraTweak(key, value, s.camera_tweaks);
        }
    }
    return true;
}

}  // namespace wasd
