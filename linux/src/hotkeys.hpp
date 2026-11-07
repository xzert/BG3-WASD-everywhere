#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "settings.hpp"

// The mod's hotkeys without the platform: key names, bindings, the keys held right now, and the
// rules for when a key event triggers a command. The Linux port feeds it SDL events (input.cpp),
// the macOS port Cocoa events. Shared by both.
namespace wasd::hotkeys {

// A key by position: a USB HID usage id, which is also an SDL scancode. Mouse buttons are numbered
// like SDL's: 1 left, 2 middle, 3 right, 4 x1, 5 x2. Shift, Ctrl and Alt mean either side.
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
    ToggleCameraRotation,
    MouselookPause,
    Forward,   // the game's CharacterMoveForward keys
    Backward,  // the game's CharacterMoveBackward keys
    kCommandCount
};

inline constexpr int kLCtrl = 224, kLShift = 225, kLAlt = 226, kLGui = 227;
inline constexpr int kRCtrl = 228, kRShift = 229, kRAlt = 230, kRGui = 231;
inline constexpr int kEscape = 41, kCapsLock = 57;
inline constexpr int kMaxScancode = 512;
inline constexpr int kMaxMouseButton = 8;

// "key:w", "mouse:x1", "shift" and so on, as in bg3-wasd-everywhere.toml.
bool ParseKey(std::string_view name, Key& key);

// "key:x,ctrl+key:y" -> two combos. Unknown key names are logged and their combo skipped.
std::vector<Combo> ParseBinding(const std::string& setting, const char* command_name);

// Replaces the bindings from the settings.
void SetBindings(const Settings& settings);

// The game's own CharacterMoveForward/Backward keys, which cancel autoforward. Defaults: W/Up, S/Down.
void SetMovementKeys(const std::vector<std::string>& forward, const std::vector<std::string>& backward);

// The keys of a command, as bound now.
const std::vector<Combo>& Bindings(Command command);

// "key:..." or "mouse:..." to a scancode or mouse button.
bool LookupKey(const std::string& name, bool& mouse_button, int& code);

// The keys and mouse buttons held right now. The platform keeps them up to date from its events,
// before it asks Matches about the same event.
void SetDown(const Key& key, bool down);
bool IsDown(const Key& key);

// Same rules as DidCommandChange on Windows: `key` must be the last key of one of the command's
// combos, its other keys must be held, and a held modifier that the combo doesn't mention blocks
// the match.
bool Matches(Command command, const Key& key);

}  // namespace wasd::hotkeys
