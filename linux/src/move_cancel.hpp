#pragma once

// A movement key cancels a click move, like HandleCharacterCommandCavehook on Windows. Shared by the
// Linux and macOS ports, which call it from their hooks of ecl::InputController::OnInputEvent.
//
// When ActionCancel (input 174) is released, InputController::OnInputEvent ends the running task or
// asks the server to stop the character: what a right click does to a click move. The hook passes
// the game such a release before the first movement key goes down. Only the first: with a key
// already held, the character moves by WASD, and a cancel would make the movement stutter.

#include <cstdint>
#include <cstring>

namespace wasd::move_cancel {

// ls::InputEvent: +0 the input id, +0x10 "was active" and +0x1C "is active" (the bytes of the old
// and the new value that the game's own handlers test). At least 40 bytes long.
constexpr size_t kEventSize = 40;
constexpr uint32_t kActionCancel = 174;

// The direction of a movement input, 0 to 3, or -1. In Character mode the movement keys fire both
// the character and the camera movement actions.
inline int Direction(uint32_t id)
{
    switch (id) {
    case 157:  // CharacterMoveForward
    case 100:  // CameraForward
        return 0;
    case 158:  // CharacterMoveBackward
    case 99:   // CameraBackward
        return 1;
    case 159:  // CharacterMoveLeft
    case 101:  // CameraLeft
        return 2;
    case 160:  // CharacterMoveRight
    case 102:  // CameraRight
        return 3;
    default:
        return -1;
    }
}

// Updates `held` (one bit per direction) from the event and returns true if the event is the first
// movement key going down, with no other one held.
inline bool FirstMovementPress(const uint8_t* event, uint8_t& held)
{
    uint32_t id;
    std::memcpy(&id, event, sizeof id);
    const int direction = Direction(id);
    if (direction < 0) {
        return false;
    }
    const bool was = event[0x10] != 0;
    const bool is = event[0x1C] != 0;
    const uint8_t bit = static_cast<uint8_t>(1u << direction);
    if (was == is) {
        return false;
    }
    if (!is) {
        held = static_cast<uint8_t>(held & ~bit);
        return false;
    }
    const bool first = held == 0;
    held = static_cast<uint8_t>(held | bit);
    return first;
}

// Builds the ActionCancel release in `out` (kEventSize bytes) from the movement event, which gives
// the device and the player fields.
inline void MakeCancelRelease(const uint8_t* event, uint8_t* out)
{
    std::memcpy(out, event, kEventSize);
    std::memcpy(out, &kActionCancel, sizeof kActionCancel);
    out[0x07] = 2;  // released
    std::memset(out + 0x08, 0, 24);
    out[0x10] = 1;  // was active
}

}  // namespace wasd::move_cancel
