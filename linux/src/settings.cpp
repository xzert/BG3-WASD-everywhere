#include "settings.hpp"

#include <sys/inotify.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>

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
// The game's first process forks the one that plays and then only waits for it, so a watcher
// thread started from the constructor would stay behind in the parent. The inotify descriptor is
// inherited instead, and the playing process's main thread reads it.
int g_watch_fd = -1;
std::string& WatchedName()
{
    static std::string name;
    return name;
}
int64_t g_last_change_ms = 0;  // the latest change not yet reloaded, 0 if none

}  // namespace

void SetSettingsPath(std::string path) { SettingsPath() = std::move(path); }

bool TakeReloadRequest()
{
    // Once a second is enough: switching from the editor back to the game takes about that long.
    // The clock is read without a system call; the read() returns at once when nothing changed.
    constexpr int64_t kCheckIntervalMs = 1000;
    static int64_t last_check_ms = 0;
    const int64_t now = NowMs();
    if (g_watch_fd < 0 || now - last_check_ms < kCheckIntervalMs) {
        return false;
    }
    last_check_ms = now;
    alignas(inotify_event) char buffer[4096];
    for (;;) {
        const ssize_t length = read(g_watch_fd, buffer, sizeof buffer);
        if (length <= 0) {
            break;
        }
        for (ssize_t offset = 0; offset < length;) {
            const auto* event = reinterpret_cast<const inotify_event*>(buffer + offset);
            if (event->len && WatchedName() == event->name) {
                g_last_change_ms = now;
            }
            offset += sizeof(inotify_event) + event->len;
        }
    }
    // Reload at the first check without new changes: editors may write the file in several steps.
    // So the config applies one to two seconds after the save.
    if (g_last_change_ms && g_last_change_ms < now) {
        g_last_change_ms = 0;
        return true;
    }
    return false;
}

void WatchSettingsFile()
{
    const std::string& path = SettingsPath();
    const auto slash = path.rfind('/');
    const std::string dir = slash == std::string::npos ? "." : path.substr(0, slash);
    WatchedName() = slash == std::string::npos ? path : path.substr(slash + 1);

    const int fd = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
    // Editors often save by writing a temporary file and renaming it, so watch the directory.
    if (fd < 0 || inotify_add_watch(fd, dir.c_str(), IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE) < 0) {
        LOG_WARN("Config changes won't be picked up automatically (inotify on %s: %s)", dir.c_str(),
            std::strerror(errno));
        if (fd >= 0) {
            close(fd);
        }
        return;
    }
    g_watch_fd = fd;
}

void ReloadSettings()
{
    Settings settings;
    const std::string& path = SettingsPath();
    const bool parsed = ParseSettingsFile(path, settings);
    log::SetDebug(settings.debug_log);
    if (parsed) {
        LOG_INFO("Config loaded from %s%s", path.c_str(), settings.debug_log ? ", debug log on" : "");
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
    state.max_click_distance_behind_character = settings.max_click_distance_behind_character;
    state.max_click_distance_toward_camera = settings.max_click_distance_toward_camera;
    state.prevent_clicking_camera_occluders = settings.prevent_clicking_camera_occluders;
    state.block_interact_move = settings.block_interact_move;
    camera::UpdateCharacterModeCamera();  // BlockInteractMove may have changed
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
