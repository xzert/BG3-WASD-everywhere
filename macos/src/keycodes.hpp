#pragma once

#include <cstdint>

// macOS virtual key codes (kVK_* in HIToolbox) and USB HID usage ids, the key positions that the
// mod's key names mean on every platform (SDL scancodes on Linux). Plain tables, so the host test
// checks them too.
namespace wasd::keycodes {

// The HID usage id of a virtual key code, or 0 for keys without one (Fn, the JIS keys).
int HidFromVirtualKey(unsigned short virtual_key);

// The virtual key code of a HID usage id, or -1 if no Mac key has it.
int VirtualKeyFromHid(int hid);

// macOS virtual key codes the library needs by name.
inline constexpr unsigned short kVirtualCapsLock = 0x39;
inline constexpr unsigned short kVirtualLeftShift = 0x38, kVirtualRightShift = 0x3C;
inline constexpr unsigned short kVirtualLeftControl = 0x3B, kVirtualRightControl = 0x3E;
inline constexpr unsigned short kVirtualLeftOption = 0x3A, kVirtualRightOption = 0x3D;
inline constexpr unsigned short kVirtualLeftCommand = 0x37, kVirtualRightCommand = 0x36;

// NSEvent modifier flags: the device-independent ones and, in the low bits, which side's key is
// down (NX_DEVICE*KEYMASK). Kept here as numbers so this file needs no Cocoa headers.
inline constexpr uint64_t kFlagCapsLock = 1 << 16, kFlagShift = 1 << 17, kFlagControl = 1 << 18;
inline constexpr uint64_t kFlagOption = 1 << 19, kFlagCommand = 1 << 20;
inline constexpr uint64_t kDeviceLeftControl = 0x1, kDeviceLeftShift = 0x2, kDeviceRightShift = 0x4;
inline constexpr uint64_t kDeviceLeftCommand = 0x8, kDeviceRightCommand = 0x10;
inline constexpr uint64_t kDeviceLeftOption = 0x20, kDeviceRightOption = 0x40, kDeviceRightControl = 0x2000;

// Which modifier keys `flags` says are down, as HID usage ids, in `down[8]`: left and right
// Control, Shift, Option (Alt), Command (GUI), in the HID order 224-231. When an event carries only
// the device-independent flag, the left key counts as down.
void ModifiersDown(uint64_t flags, bool down[8]);

}  // namespace wasd::keycodes
