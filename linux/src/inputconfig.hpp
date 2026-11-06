#pragma once

#include <string>
#include <vector>

// The game's key bindings (inputconfig_p1.json in each player profile), patched like InputconfigPatcher
// on Windows: CharacterMoveForward/Backward/Left/Right get the keys of CameraForward/Backward/Left/Right
// (or the game's defaults). The library runs before the game reads the file, so patching it at
// startup is enough; Windows also re-patches after keybinding changes in the menu.
namespace wasd::inputconfig {

struct Result {
    bool ok = false;
    bool changed = false;
    std::string error;
    std::vector<std::string> forward_keys;   // keyboard and mouse keys of CharacterMoveForward
    std::vector<std::string> backward_keys;  // ... of CharacterMoveBackward
    bool rotate_includes_left_click = false;  // CameraToggleMouseRotate is bound to the left button
};

// Patches the JSON text (empty text counts as an empty object).
Result PatchText(const std::string& in, std::string& out);

// Patches every PlayerProfiles/*/inputconfig_p1.json, creating missing ones. Keeps a one-time backup
// (inputconfig_p1.json.bg3wasd.bak) and rewrites the file atomically. The result describes the
// "Public" profile, or the first one found.
Result PatchAllProfiles();

}  // namespace wasd::inputconfig
