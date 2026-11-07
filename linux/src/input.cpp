#include "input.hpp"

#include <dlfcn.h>

#include <atomic>
#include <cstdint>
#include <cstring>

#include "camera.hpp"
#include "hotkeys.hpp"
#include "log.hpp"
#include "state.hpp"

namespace wasd::input {
namespace {

using hotkeys::Command;
using hotkeys::Key;

// SDL2 event ABI (stable across 2.x): event types and the field offsets read below.
constexpr uint32_t kKeyDown = 0x300;
constexpr uint32_t kKeyUp = 0x301;
constexpr uint32_t kMouseButtonDown = 0x401;
constexpr uint32_t kMouseButtonUp = 0x402;
constexpr size_t kKeyRepeatOffset = 13;
constexpr size_t kKeyScancodeOffset = 16;
constexpr size_t kMouseButtonOffset = 16;

std::atomic<bool> g_enabled{ false };

void OnKey(const Key& key, bool down)
{
    State& state = GetState();
    auto pressed = [&](Command c) { return down && hotkeys::Matches(c, key); };
    auto released = [&](Command c) { return !down && hotkeys::Matches(c, key); };

    if (pressed(hotkeys::Forward) || pressed(hotkeys::Backward) || pressed(hotkeys::ToggleMovementMode) ||
        pressed(hotkeys::HoldMovementMode) || released(hotkeys::HoldMovementMode)) {
        if (state.autoforward.exchange(false)) {
            LOG_INFO("Autoforward off");
        }
    }

    if (pressed(hotkeys::ToggleAutoforward) && state.IsCharacterMovementMode()) {
        const bool on = !state.autoforward;
        state.autoforward = on;
        LOG_INFO("Autoforward %s", on ? "on" : "off");
    }

    if (pressed(hotkeys::ToggleMovementMode)) {
        state.SetMovementModeToggled(!state.movement_mode_toggled);
    } else if (pressed(hotkeys::HoldMovementMode)) {
        state.SetMovementModeHeld(true);
    } else if (released(hotkeys::HoldMovementMode)) {
        state.SetMovementModeHeld(false);
    }

    if (state.IsCharacterMovementMode()) {
        if (pressed(hotkeys::ToggleWalkspeed)) {
            const bool walking = !state.walking_toggled;
            state.walking_toggled = walking;
            LOG_INFO("Walking %s", walking ? "on" : "off");
        } else if (pressed(hotkeys::HoldWalkspeed)) {
            state.walking_held = true;
        } else if (released(hotkeys::HoldWalkspeed)) {
            state.walking_held = false;
        }
    }

    if (pressed(hotkeys::ReloadConfig)) {
        ReloadSettings();
    }

    // Panels (inventory, map, ...) keep the game taking movement input, so mouselook can't see them;
    // their keys pause it instead. Esc closes panels.
    if (pressed(hotkeys::MouselookPause)) {
        camera::OnPanelKey(key.kind == Key::MouseButton ? -key.code : key.code);
    }
    if (down && key.kind == Key::Scancode && key.code == hotkeys::kEscape) {
        camera::OnCloseAllPanels();
    }

    if (pressed(hotkeys::ToggleCameraRotation)) {
        camera::ToggleMouselook();
    }
}

// The game starts and stops SDL text input itself, so this is true while a text field has focus.
bool TextInputActive()
{
    using IsTextInputActiveFn = int (*)();
    static const auto fn = reinterpret_cast<IsTextInputActiveFn>(dlsym(RTLD_NEXT, "SDL_IsTextInputActive"));
    return fn && fn();
}

void OnSdlEvent(const void* event)
{
    const auto* raw = static_cast<const uint8_t*>(event);
    uint32_t type;
    std::memcpy(&type, raw, sizeof type);

    if (type == kKeyDown || type == kKeyUp) {
        int32_t scancode;
        std::memcpy(&scancode, raw + kKeyScancodeOffset, sizeof scancode);
        if (scancode < 0 || scancode >= hotkeys::kMaxScancode) {
            return;
        }
        const bool down = type == kKeyDown;
        const Key key{ Key::Scancode, scancode };
        const bool was_down = hotkeys::IsDown(key);
        hotkeys::SetDown(key, down);
        if (down && (raw[kKeyRepeatOffset] || was_down)) {
            return;  // auto-repeat
        }
        camera::OnInputEvent(false, scancode, down);
        // Typing into a text field (a save name, chat) must not toggle modes or autoforward.
        // Releases still count, so a held key can't get stuck.
        if (!down || !TextInputActive()) {
            OnKey(key, down);
        }
    } else if (type == kMouseButtonDown || type == kMouseButtonUp) {
        const uint8_t button = raw[kMouseButtonOffset];
        if (button >= hotkeys::kMaxMouseButton) {
            return;
        }
        const bool down = type == kMouseButtonDown;
        const Key key{ Key::MouseButton, button };
        hotkeys::SetDown(key, down);
        if (button == 1) {
            GetState().mouse_left_down = down;
        }
        camera::OnInputEvent(true, button, down);
        OnKey(key, down);
    }
}

}  // namespace

void SetBindings(const Settings& settings) { hotkeys::SetBindings(settings); }

void SetMovementKeys(const std::vector<std::string>& forward, const std::vector<std::string>& backward)
{
    hotkeys::SetMovementKeys(forward, backward);
}

bool LookupKey(const std::string& name, bool& mouse_button, int& code)
{
    return hotkeys::LookupKey(name, mouse_button, code);
}

void Enable() { g_enabled = true; }

}  // namespace wasd::input

// The game calls SDL_PollEvent through its PLT, so this definition, preloaded ahead of libSDL2.so,
// receives every call.
extern "C" __attribute__((visibility("default"))) int SDL_PollEvent(void* event)
{
    using PollEventFn = int (*)(void*);
    static const auto real = reinterpret_cast<PollEventFn>(dlsym(RTLD_NEXT, "SDL_PollEvent"));
    if (!real) {
        return 0;
    }
    // The game polls in a loop until no events are left, so the first call of each loop starts a
    // frame. (On Windows, PollEventHook hooks that first call.) Only the main thread polls.
    static bool polling = false;
    if (!polling) {
        polling = true;
        if (wasd::TakeReloadRequest()) {
            wasd::ReloadSettings();
        }
        wasd::camera::OnFrameStart();
    }
    const int result = real(event);
    if (result == 0) {
        polling = false;
    }
    if (result == 1 && event && wasd::input::g_enabled) {
        wasd::input::OnSdlEvent(event);
    }
    return result;
}
