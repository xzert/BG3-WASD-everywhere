#include "settings.hpp"

#include <poll.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "camera.hpp"
#include "camera_tweaks.hpp"
#include "input.hpp"
#include "log.hpp"
#include "state.hpp"

namespace wasd {
namespace {

// A function-local static: the constructor in main.cpp that starts the mod can run before this
// file's static initializers, which would then reset the path it set.
std::string& SettingsPath()
{
    static std::string path;
    return path;
}

bool g_loaded_once = false;
std::atomic<bool> g_reload_requested{ false };

}  // namespace

void SetSettingsPath(std::string path) { SettingsPath() = std::move(path); }

bool TakeReloadRequest() { return g_reload_requested.exchange(false); }

void WatchSettingsFile()
{
    const std::string& path = SettingsPath();
    const auto slash = path.rfind('/');
    const std::string dir = slash == std::string::npos ? "." : path.substr(0, slash);
    const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);

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
    const std::string& path = SettingsPath();
    if (ParseSettingsFile(path, settings)) {
        LOG_INFO("Config loaded from %s", path.c_str());
    } else {
        LOG_WARN("Could not read %s, using defaults", path.c_str());
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
    state.mode_toggles_mouselook = settings.toggling_movement_mode_also_toggles_camera_rotation;
    state.prevent_clicking_active_character = settings.prevent_clicking_active_character;
    state.prevent_clicking_camera_occluders = settings.prevent_clicking_camera_occluders;
    camera_tweaks::Configure(settings.camera_tweaks);
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
