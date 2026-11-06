#pragma once

#include <string>
#include <vector>

#include "settings.hpp"

// Mod hotkeys. The Windows mod uses low-level keyboard and mouse hooks; here the library interposes
// SDL_PollEvent, which the game imports from libSDL2.so, and watches the events the game receives.
namespace wasd::input {

// Replaces the hotkey bindings. Unknown key names are logged and skipped.
void SetBindings(const Settings& settings);

// The game's own CharacterMoveForward/Backward keys, which cancel autoforward. Defaults: W/Up, S/Down.
void SetMovementKeys(const std::vector<std::string>& forward, const std::vector<std::string>& backward);

// Starts reacting to SDL events.
void Enable();

}  // namespace wasd::input
