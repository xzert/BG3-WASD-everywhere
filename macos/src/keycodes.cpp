#include "keycodes.hpp"

namespace wasd::keycodes {
namespace {

// kVK_* → HID usage id, indexed by the virtual key code (0x00-0x7F).
constexpr int kHidByVirtualKey[128] = {
    // 0x00: A S D F H G Z X
    4, 22, 7, 9, 11, 10, 29, 27,
    // 0x08: C V ISO_Section B Q W E R
    6, 25, 100, 5, 20, 26, 8, 21,
    // 0x10: Y T 1 2 3 4 6 5
    28, 23, 30, 31, 32, 33, 35, 34,
    // 0x18: = 9 7 - 8 0 ] O
    46, 38, 36, 45, 37, 39, 48, 18,
    // 0x20: U [ I P Return L J '
    24, 47, 12, 19, 40, 15, 13, 52,
    // 0x28: K ; \ , / N M .
    14, 51, 49, 54, 56, 17, 16, 55,
    // 0x30: Tab Space ` Delete (none) Escape RightCommand Command
    43, 44, 53, 42, 0, 41, 231, 227,
    // 0x38: Shift CapsLock Option Control RightShift RightOption RightControl Function
    225, 57, 226, 224, 229, 230, 228, 0,
    // 0x40: F17 Keypad. (none) Keypad* (none) Keypad+ (none) KeypadClear
    108, 99, 0, 85, 0, 87, 0, 83,
    // 0x48: VolumeUp VolumeDown Mute Keypad/ KeypadEnter (none) Keypad- F18
    128, 129, 127, 84, 88, 0, 86, 109,
    // 0x50: F19 Keypad= Keypad0 Keypad1 Keypad2 Keypad3 Keypad4 Keypad5
    110, 103, 98, 89, 90, 91, 92, 93,
    // 0x58: Keypad6 Keypad7 F20 Keypad8 Keypad9 JIS_Yen JIS_Underscore JIS_KeypadComma
    94, 95, 111, 96, 97, 0, 0, 0,
    // 0x60: F5 F6 F7 F3 F8 F9 JIS_Eisu F11
    62, 63, 64, 60, 65, 66, 0, 68,
    // 0x68: JIS_Kana F13 F16 F14 (none) F10 ContextualMenu F12
    0, 104, 107, 105, 0, 67, 101, 69,
    // 0x70: (none) F15 Help(Insert) Home PageUp ForwardDelete F4 End
    0, 106, 73, 74, 75, 76, 61, 77,
    // 0x78: F2 PageDown F1 Left Right Down Up (none)
    59, 78, 58, 80, 79, 81, 82, 0,
};

}  // namespace

int HidFromVirtualKey(unsigned short virtual_key)
{
    return virtual_key < 128 ? kHidByVirtualKey[virtual_key] : 0;
}

int VirtualKeyFromHid(int hid)
{
    if (hid <= 0) {
        return -1;
    }
    for (int key = 0; key < 128; ++key) {
        if (kHidByVirtualKey[key] == hid) {
            return key;
        }
    }
    return -1;
}

void ModifiersDown(uint64_t flags, bool down[8])
{
    struct Side {
        uint64_t any, left, right;
    };
    // HID order: Control, Shift, Option (Alt), Command (GUI); left keys at 0-3, right keys at 4-7.
    constexpr Side kSides[4] = {
        { kFlagControl, kDeviceLeftControl, kDeviceRightControl },
        { kFlagShift, kDeviceLeftShift, kDeviceRightShift },
        { kFlagOption, kDeviceLeftOption, kDeviceRightOption },
        { kFlagCommand, kDeviceLeftCommand, kDeviceRightCommand },
    };
    for (int i = 0; i < 4; ++i) {
        const bool any = flags & kSides[i].any;
        bool left = any && (flags & kSides[i].left);
        const bool right = any && (flags & kSides[i].right);
        if (any && !left && !right) {
            left = true;
        }
        down[i] = left;
        down[i + 4] = right;
    }
}

}  // namespace wasd::keycodes
