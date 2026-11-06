#include "settings.hpp"

#include <poll.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <thread>

#include "camera.hpp"
#include "input.hpp"
#include "log.hpp"
#include "state.hpp"

namespace wasd {
namespace {

std::string g_path;
bool g_loaded_once = false;
std::atomic<bool> g_reload_requested{ false };

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
        } else if (key == "AutoToggleMovementMode") {
            s.auto_toggle_movement_mode = ParseBool(value, s.auto_toggle_movement_mode);
        } else if (key == "SwitchToWalkingAfterCombat") {
            s.walk_after_combat = ParseBool(value, s.walk_after_combat);
        } else if (key == "RotatePlusLeftclickMovesForward") {
            s.rotate_plus_lmb_is_forward = ParseBool(value, s.rotate_plus_lmb_is_forward);
        }
    }
    return true;
}

void SetSettingsPath(std::string path) { g_path = std::move(path); }

bool TakeReloadRequest() { return g_reload_requested.exchange(false); }

void WatchSettingsFile()
{
    const auto slash = g_path.rfind('/');
    const std::string dir = slash == std::string::npos ? "." : g_path.substr(0, slash);
    const std::string name = slash == std::string::npos ? g_path : g_path.substr(slash + 1);

    const int fd = inotify_init1(IN_CLOEXEC);
    // Editors often save by writing a temporary file and renaming it, so watch the directory.
    if (fd < 0 || inotify_add_watch(fd, dir.c_str(), IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE) < 0) {
        LOG_WARN("Config changes won't be picked up automatically (inotify on %s: %s)", dir.c_str(),
            std::strerror(errno));
        if (fd >= 0) {
            close(fd);
        }
        return;
    }

    std::thread([fd, name] {
        constexpr int kDebounceMs = 1000;  // reload once the file has been quiet for a second
        alignas(inotify_event) char buffer[4096];
        bool pending = false;
        for (;;) {
            pollfd poll_fd{ fd, POLLIN, 0 };
            const int ready = poll(&poll_fd, 1, pending ? kDebounceMs : -1);
            if (ready == 0) {
                pending = false;
                g_reload_requested = true;
                continue;
            }
            if (ready < 0) {
                if (errno == EINTR) {
                    continue;
                }
                break;
            }
            const ssize_t length = read(fd, buffer, sizeof buffer);
            for (ssize_t offset = 0; offset < length;) {
                const auto* event = reinterpret_cast<const inotify_event*>(buffer + offset);
                if (event->len && name == event->name) {
                    pending = true;
                }
                offset += sizeof(inotify_event) + event->len;
            }
        }
        LOG_WARN("Stopped watching the config file");
    }).detach();
}

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
    state.auto_toggle_movement_mode = settings.auto_toggle_movement_mode;
    state.walk_after_combat = settings.walk_after_combat;
    state.rotate_plus_lmb_is_forward = settings.rotate_plus_lmb_is_forward;
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
