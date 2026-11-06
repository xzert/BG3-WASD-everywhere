#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace wasd {

// Game addresses the mod needs, found by signature.
struct GameAddresses {
    // `cmp byte ptr [IsInControllerMode], 0 ; je` in UpdateWasdMove2 (kWasdUnlockSize bytes).
    uintptr_t wasd_unlock = 0;
    // The bool global that cmp reads.
    uintptr_t is_in_controller_mode = 0;
    // The four `call GetInputValue` in GetMovementInputWorldVec, for command ids 0xA0, 0x9F, 0x9D, 0x9E.
    std::array<uintptr_t, 4> get_input_value_calls{};
    uintptr_t get_input_value = 0;
};

constexpr size_t kWasdUnlockSize = 13;

// Searches the game's code. `text_address` is the address that `text[0]` corresponds to, so the
// same search runs on live memory (in the game) and on the file (in selftest).
bool FindGameAddresses(const uint8_t* text, size_t size, uintptr_t text_address,
    GameAddresses& out, std::string& error);

// ExecuteCameraCommand(this in rdi, context in rsi, Command* in rdx). The game calls it through a
// vtable, so the mod hooks it by replacing its vtable slot. Optional: movement works without it.
bool FindExecuteCameraCommand(const uint8_t* text, size_t size, uintptr_t text_address,
    uintptr_t& out, std::string& error);

// Camera rotation. Rotation pushes the rotation cursor (id 0x2E) onto the cursor manager when it
// starts and pops it when it stops; the Windows mod calls these CameraRotationOn/Off.
struct RotationAddresses {
    uintptr_t cursor_push = 0;
    uintptr_t cursor_pop = 0;
    std::vector<uintptr_t> push_calls;  // `mov esi, 0x2E ; call cursor_push`
    std::vector<uintptr_t> pop_calls;   // `mov esi, 0x2E ; call cursor_pop`
    // `call qword ptr [rax+slot]` (6 bytes) to SetVirtualCursorPos in the SDL event loop.
    uintptr_t set_virtual_cursor_call = 0;
    uint32_t set_virtual_cursor_slot = 0;
};

bool FindRotationAddresses(const uint8_t* text, size_t size, uintptr_t text_address,
    uintptr_t execute_camera_command, RotationAddresses& out, std::string& error);

}  // namespace wasd
