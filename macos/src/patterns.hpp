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

// Camera tweaks (Native Camera Tweaks). Each of them replaces 16 bytes of game code with a jump
// to a stub in stubs.S that does the same instructions, so they must be exactly these.

// In UpdateGameCameraBehavior, the call that asks for the pitch the camera turns to:
// `mov x0, x26 ; mov w1, #0 ; bl GetCameraPitchDegrees ; ldr s1, [x26, #0x164]`. Before it, the
// code picks the camera definition from eoc::CameraSettings: `adrp ; ldr` of the global pointer,
// `mov w9, #flag`, then `add` of the override, exploration and combat definition offsets.
struct PitchUpdate {
    long index = -1;  // of the `mov x0, x26`
    uint64_t settings_pointer = 0;
    uint32_t override_flag = 0;
    uint32_t exploration = 0;
    uint32_t combat = 0;
    uint32_t override_definition = 0;
};
bool FindPitchUpdate(const uint32_t* code, size_t count, uint64_t pc, uint64_t pitch_function, PitchUpdate& out);

// In ecl::CameraSystem::OnInputEvent, where the zoom command writes its speed into the camera:
// `fmul s1, s1, s2 ; fmul s0, s1, s0 ; str s0, [x22, #0xA4] ; b next`. Returns the index of the
// first fmul and sets `next`, or -1.
long FindZoomSpeedStore(const uint32_t* code, size_t count, uint64_t pc, uint64_t& next);

// The same for the keyboard rotation commands:
// `fmov s2, w8 ; fmul s1, s1, s2 ; fmul s0, s1, s0 ; str s0, [x22, #0x9C]`. Returns the index of
// the fmov, or -1.
long FindRotationSpeedStore(const uint32_t* code, size_t count, uint64_t pc);

}  // namespace wasd::patterns
