#include "input.hpp"

#include <dlfcn.h>

#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "camera.hpp"
#include "log.hpp"
#include "state.hpp"

namespace wasd::input {
namespace {

// SDL2 event ABI (stable across 2.x): event types and the field offsets read below.
constexpr uint32_t kKeyDown = 0x300;
constexpr uint32_t kKeyUp = 0x301;
constexpr uint32_t kMouseButtonDown = 0x401;
constexpr uint32_t kMouseButtonUp = 0x402;
constexpr size_t kKeyRepeatOffset = 13;
constexpr size_t kKeyScancodeOffset = 16;
constexpr size_t kMouseButtonOffset = 16;

constexpr int kLCtrl = 224, kLShift = 225, kLAlt = 226, kRCtrl = 228, kRShift = 229, kRAlt = 230;

struct Key {
    enum Kind : uint8_t { Scancode, MouseButton, Shift, Ctrl, Alt } kind;
    int code = 0;
};
using Combo = std::vector<Key>;  // modifiers or held keys first, the triggering key last

enum Command {
    ToggleMovementMode,
    HoldMovementMode,
    ToggleAutoforward,
    ToggleWalkspeed,
    HoldWalkspeed,
    ReloadConfig,
    Forward,
    Backward,
    kCommandCount
};

struct NamedKey {
    const char* name;
    int code;
};
constexpr NamedKey kKeyNames[] = {
#include "keymap.inc"
};
constexpr NamedKey kMouseButtons[] = {
    { "mouse:left", 1 }, { "mouse:middle", 2 }, { "mouse:right", 3 }, { "mouse:x1", 4 }, { "mouse:x2", 5 }
};

std::vector<Combo> g_bindings[kCommandCount];
std::string g_forward_keys = "key:w,key:up";
std::string g_backward_keys = "key:s,key:down";
bool g_scancode_down[512] = {};
bool g_mouse_down[8] = {};
std::atomic<bool> g_enabled{ false };

bool ParseKey(std::string_view name, Key& key)
{
    if (name == "shift" || name == "ctrl" || name == "alt") {
        key = { name == "shift" ? Key::Shift : name == "ctrl" ? Key::Ctrl : Key::Alt };
        return true;
    }
    // Letters and digits: SDL scancodes follow USB HID usage ids.
    if (name.size() == 5 && name.substr(0, 4) == "key:") {
        const char c = name[4];
        if (c >= 'a' && c <= 'z') {
            key = { Key::Scancode, 4 + (c - 'a') };
            return true;
        }
        if (c >= '1' && c <= '9') {
            key = { Key::Scancode, 30 + (c - '1') };
            return true;
        }
        if (c == '0') {
            key = { Key::Scancode, 39 };
            return true;
        }
    }
    for (const auto& button : kMouseButtons) {
        if (name == button.name) {
            key = { Key::MouseButton, button.code };
            return true;
        }
    }
    for (const auto& named : kKeyNames) {
        if (name == named.name) {
            key = { Key::Scancode, named.code };
            return true;
        }
    }
    return false;
}

// "key:x,ctrl+key:y" -> two combos.
std::vector<Combo> ParseBinding(const std::string& setting, const char* command_name)
{
    std::string text;
    for (char c : setting) {
        if (c != ' ') {
            text += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }

    std::vector<Combo> combos;
    for (size_t start = 0; start <= text.size();) {
        size_t end = text.find(',', start);
        end = end == std::string::npos ? text.size() : end;
        const std::string_view combo_text(text.data() + start, end - start);
        start = end + 1;
        if (combo_text.empty()) {
            continue;
        }

        Combo combo;
        bool valid = true;
        for (size_t key_start = 0; key_start <= combo_text.size() && valid;) {
            size_t key_end = combo_text.find('+', key_start);
            key_end = key_end == std::string_view::npos ? combo_text.size() : key_end;
            const std::string_view name = combo_text.substr(key_start, key_end - key_start);
            key_start = key_end + 1;

            Key key{};
            valid = ParseKey(name, key);
            if (valid) {
                combo.push_back(key);
            } else {
                LOG_WARN("%s: unknown key \"%.*s\" in \"%s\"", command_name, static_cast<int>(name.size()),
                    name.data(), setting.c_str());
            }
        }
        if (valid && !combo.empty()) {
            combos.push_back(std::move(combo));
        }
    }
    return combos;
}

bool ShiftDown() { return g_scancode_down[kLShift] || g_scancode_down[kRShift]; }
bool CtrlDown() { return g_scancode_down[kLCtrl] || g_scancode_down[kRCtrl]; }
bool AltDown() { return g_scancode_down[kLAlt] || g_scancode_down[kRAlt]; }

bool IsDown(const Key& key)
{
    switch (key.kind) {
    case Key::Shift:
        return ShiftDown();
    case Key::Ctrl:
        return CtrlDown();
    case Key::Alt:
        return AltDown();
    case Key::MouseButton:
        return g_mouse_down[key.code];
    case Key::Scancode:
        return g_scancode_down[key.code];
    }
    return false;
}

// Same rules as DidCommandChange on Windows: the event key must be the combo's last key, the other
// keys must be held, and a held modifier that the combo doesn't mention blocks the match.
bool Matches(Command command, const Key& event_key)
{
    for (const Combo& combo : g_bindings[command]) {
        const Key& trigger = combo.back();
        if (trigger.kind != event_key.kind || trigger.code != event_key.code) {
            continue;
        }
        bool held = true, has_shift = false, has_ctrl = false, has_alt = false;
        for (size_t i = 0; i < combo.size(); ++i) {
            const Key& key = combo[i];
            const bool scancode = key.kind == Key::Scancode;
            has_shift |= key.kind == Key::Shift || (scancode && (key.code == kLShift || key.code == kRShift));
            has_ctrl |= key.kind == Key::Ctrl || (scancode && (key.code == kLCtrl || key.code == kRCtrl));
            has_alt |= key.kind == Key::Alt || (scancode && (key.code == kLAlt || key.code == kRAlt));
            if (i + 1 < combo.size()) {
                held &= IsDown(key);
            }
        }
        if (held && (has_shift || !ShiftDown()) && (has_ctrl || !CtrlDown()) && (has_alt || !AltDown())) {
            return true;
        }
    }
    return false;
}

void OnKey(const Key& key, bool down)
{
    State& state = GetState();
    auto pressed = [&](Command c) { return down && Matches(c, key); };
    auto released = [&](Command c) { return !down && Matches(c, key); };

    if (pressed(Forward) || pressed(Backward) || pressed(ToggleMovementMode) || pressed(HoldMovementMode) ||
        released(HoldMovementMode)) {
        if (state.autoforward.exchange(false)) {
            LOG_INFO("Autoforward off");
        }
    }

    if (pressed(ToggleAutoforward) && state.IsCharacterMovementMode()) {
        const bool on = !state.autoforward;
        state.autoforward = on;
        LOG_INFO("Autoforward %s", on ? "on" : "off");
    }

    if (pressed(ToggleMovementMode)) {
        state.SetMovementModeToggled(!state.movement_mode_toggled);
    } else if (pressed(HoldMovementMode)) {
        state.SetMovementModeHeld(true);
    } else if (released(HoldMovementMode)) {
        state.SetMovementModeHeld(false);
    }

    if (state.IsCharacterMovementMode()) {
        if (pressed(ToggleWalkspeed)) {
            const bool walking = !state.walking_toggled;
            state.walking_toggled = walking;
            LOG_INFO("Walking %s", walking ? "on" : "off");
        } else if (pressed(HoldWalkspeed)) {
            state.walking_held = true;
        } else if (released(HoldWalkspeed)) {
            state.walking_held = false;
        }
    }

    if (pressed(ReloadConfig)) {
        ReloadSettings();
    }
}

void OnSdlEvent(const void* event)
{
    const auto* raw = static_cast<const uint8_t*>(event);
    uint32_t type;
    std::memcpy(&type, raw, sizeof type);

    if (type == kKeyDown || type == kKeyUp) {
        int32_t scancode;
        std::memcpy(&scancode, raw + kKeyScancodeOffset, sizeof scancode);
        if (scancode < 0 || scancode >= 512) {
            return;
        }
        const bool down = type == kKeyDown;
        const bool was_down = g_scancode_down[scancode];
        g_scancode_down[scancode] = down;
        if (down && (raw[kKeyRepeatOffset] || was_down)) {
            return;  // auto-repeat
        }
        camera::OnInputEvent(false, scancode, down);
        OnKey({ Key::Scancode, scancode }, down);
    } else if (type == kMouseButtonDown || type == kMouseButtonUp) {
        const uint8_t button = raw[kMouseButtonOffset];
        if (button >= 8) {
            return;
        }
        const bool down = type == kMouseButtonDown;
        g_mouse_down[button] = down;
        if (button == 1) {
            GetState().mouse_left_down = down;
        }
        camera::OnInputEvent(true, button, down);
        OnKey({ Key::MouseButton, button }, down);
    }
}

}  // namespace

void SetBindings(const Settings& s)
{
    g_bindings[ToggleMovementMode] = ParseBinding(s.toggle_movement_mode, "ToggleMovementMode");
    g_bindings[HoldMovementMode] = ParseBinding(s.hold_movement_mode, "HoldMovementMode");
    g_bindings[ToggleAutoforward] = ParseBinding(s.toggle_autoforward, "ToggleAutoforward");
    g_bindings[ToggleWalkspeed] = ParseBinding(s.toggle_walkspeed, "ToggleWalkspeed");
    g_bindings[HoldWalkspeed] = ParseBinding(s.hold_walkspeed, "HoldWalkspeed");
    g_bindings[ReloadConfig] = ParseBinding(s.reload_config, "ReloadConfig");
    g_bindings[Forward] = ParseBinding(g_forward_keys, "CharacterMoveForward");
    g_bindings[Backward] = ParseBinding(g_backward_keys, "CharacterMoveBackward");
}

void SetMovementKeys(const std::vector<std::string>& forward, const std::vector<std::string>& backward)
{
    auto join = [](const std::vector<std::string>& keys) {
        std::string joined;
        for (const std::string& key : keys) {
            joined += (joined.empty() ? "" : ",") + key;
        }
        return joined;
    };
    if (!forward.empty()) {
        g_forward_keys = join(forward);
    }
    if (!backward.empty()) {
        g_backward_keys = join(backward);
    }
    g_bindings[Forward] = ParseBinding(g_forward_keys, "CharacterMoveForward");
    g_bindings[Backward] = ParseBinding(g_backward_keys, "CharacterMoveBackward");
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
