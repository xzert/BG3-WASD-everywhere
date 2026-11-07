#include "hotkeys.hpp"

#include <cctype>

#include "log.hpp"

namespace wasd::hotkeys {
namespace {

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

// A function-local static: the library's constructor can run before this file's static
// initializers, which would then reset the bindings it set.
struct Store {
    std::vector<Combo> bindings[kCommandCount];
    std::string forward_keys = "key:w,key:up";
    std::string backward_keys = "key:s,key:down";
};

Store& GetStore()
{
    static Store store;
    return store;
}

bool g_scancode_down[kMaxScancode] = {};
bool g_mouse_down[kMaxMouseButton] = {};

bool ShiftDown() { return g_scancode_down[kLShift] || g_scancode_down[kRShift]; }
bool CtrlDown() { return g_scancode_down[kLCtrl] || g_scancode_down[kRCtrl]; }
bool AltDown() { return g_scancode_down[kLAlt] || g_scancode_down[kRAlt]; }

}  // namespace

bool ParseKey(std::string_view name, Key& key)
{
    if (name == "shift" || name == "ctrl" || name == "alt") {
        key = { name == "shift" ? Key::Shift : name == "ctrl" ? Key::Ctrl : Key::Alt };
        return true;
    }
    // Letters and digits: USB HID usage ids, which SDL scancodes follow.
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

void SetBindings(const Settings& s)
{
    Store& store = GetStore();
    store.bindings[ToggleMovementMode] = ParseBinding(s.toggle_movement_mode, "ToggleMovementMode");
    store.bindings[HoldMovementMode] = ParseBinding(s.hold_movement_mode, "HoldMovementMode");
    store.bindings[ToggleAutoforward] = ParseBinding(s.toggle_autoforward, "ToggleAutoforward");
    store.bindings[ToggleWalkspeed] = ParseBinding(s.toggle_walkspeed, "ToggleWalkspeed");
    store.bindings[HoldWalkspeed] = ParseBinding(s.hold_walkspeed, "HoldWalkspeed");
    store.bindings[ReloadConfig] = ParseBinding(s.reload_config, "ReloadConfig");
    store.bindings[ToggleCameraRotation] = ParseBinding(s.toggle_camerarotation, "ToggleCameraRotation");
    store.bindings[MouselookPause] = ParseBinding(s.mouselook_pause_keys, "MouselookPauseKeys");
    store.bindings[Forward] = ParseBinding(store.forward_keys, "CharacterMoveForward");
    store.bindings[Backward] = ParseBinding(store.backward_keys, "CharacterMoveBackward");
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
    Store& store = GetStore();
    if (!forward.empty()) {
        store.forward_keys = join(forward);
    }
    if (!backward.empty()) {
        store.backward_keys = join(backward);
    }
    store.bindings[Forward] = ParseBinding(store.forward_keys, "CharacterMoveForward");
    store.bindings[Backward] = ParseBinding(store.backward_keys, "CharacterMoveBackward");
}

const std::vector<Combo>& Bindings(Command command) { return GetStore().bindings[command]; }

bool LookupKey(const std::string& name, bool& mouse_button, int& code)
{
    Key key{};
    if (!ParseKey(name, key) || (key.kind != Key::Scancode && key.kind != Key::MouseButton)) {
        return false;
    }
    mouse_button = key.kind == Key::MouseButton;
    code = key.code;
    return true;
}

void SetDown(const Key& key, bool down)
{
    if (key.kind == Key::Scancode && key.code >= 0 && key.code < kMaxScancode) {
        g_scancode_down[key.code] = down;
    } else if (key.kind == Key::MouseButton && key.code >= 0 && key.code < kMaxMouseButton) {
        g_mouse_down[key.code] = down;
    }
}

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
        return key.code >= 0 && key.code < kMaxMouseButton && g_mouse_down[key.code];
    case Key::Scancode:
        return key.code >= 0 && key.code < kMaxScancode && g_scancode_down[key.code];
    }
    return false;
}

bool Matches(Command command, const Key& key)
{
    for (const Combo& combo : GetStore().bindings[command]) {
        const Key& trigger = combo.back();
        if (trigger.kind != key.kind || trigger.code != key.code) {
            continue;
        }
        bool held = true, has_shift = false, has_ctrl = false, has_alt = false;
        for (size_t i = 0; i < combo.size(); ++i) {
            const Key& part = combo[i];
            const bool scancode = part.kind == Key::Scancode;
            has_shift |= part.kind == Key::Shift || (scancode && (part.code == kLShift || part.code == kRShift));
            has_ctrl |= part.kind == Key::Ctrl || (scancode && (part.code == kLCtrl || part.code == kRCtrl));
            has_alt |= part.kind == Key::Alt || (scancode && (part.code == kLAlt || part.code == kRAlt));
            if (i + 1 < combo.size()) {
                held &= IsDown(part);
            }
        }
        if (held && (has_shift || !ShiftDown()) && (has_ctrl || !CtrlDown()) && (has_alt || !AltDown())) {
            return true;
        }
    }
    return false;
}

}  // namespace wasd::hotkeys
