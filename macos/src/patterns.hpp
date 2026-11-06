#pragma once

#include <cstddef>
#include <cstdint>

// Where in the game's arm64 code the port patches. The functions are found by name; these find the
// instructions inside them. Plain integer code, so the host test checks it against the game binary.
namespace wasd::patterns {

// ecl::EocClient::SetKeyboardMode(bool) clears the game's controller-mode flag with `strb wzr`.
// Returns the flag's address, or 0 unless the function clears exactly one byte that way.
uint64_t FindControllerModeFlag(const uint32_t* code, size_t count, uint64_t pc);

// ecl::CharacterTask_MoveController::CanExecute() gives up when the flag is 0: an `ldrb` of the
// flag followed by `cbz` on the loaded register. Returns the index of that cbz, or -1 unless there
// is exactly one. Turning it into a nop lets the keyboard drive the gamepad movement task, like
// WASDUnlockPatch on Windows.
long FindControllerModeCheck(const uint32_t* code, size_t count, uint64_t pc, uint64_t flag);

}  // namespace wasd::patterns
