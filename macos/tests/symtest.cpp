// Host check of the macOS port against the game's macOS executable. It finds the library's symbols
// in both slices the way the library does in memory, checks the ecl::CameraSystem vtable slot and
// that the hooked code can be hooked, and finds the WASD unlock. It also checks the key code tables
// and the hotkey and config code shared with the Linux port. Builds on Linux and macOS.
//
// Usage: symtest <path to the "Baldur's Gate 3" executable>

#include <unistd.h>

#include <cinttypes>
#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "arm64.hpp"
#include "hotkeys.hpp"
#include "keycodes.hpp"
#include "macho.hpp"
#include "patterns.hpp"
#include "settings.hpp"
#include "symbols.hpp"

using namespace wasd;

namespace {

bool CheckEncodings()
{
    struct Case {
        uint32_t instruction;
        bool pc_relative;
    };
    // Taken from the game: tbz, ret, b, b.eq, cmn, stp, stp (SIMD), adrp, ldr (literal).
    const Case cases[] = { { 0x36000042, true }, { 0xD65F03C0, false }, { 0x140024D7, true },
        { 0x54000820, true }, { 0x3100045F, false }, { 0xA9BE4FF4, false }, { 0x6DBB2BEB, false },
        { 0x90000010, true }, { 0x58000050, true } };
    bool ok = true;
    for (const Case& c : cases) {
        if (arm64::IsPcRelative(c.instruction) != c.pc_relative) {
            std::printf("IsPcRelative(%08x) is wrong\n", c.instruction);
            ok = false;
        }
    }
    uint32_t jump[4];
    arm64::EncodeAbsoluteJump(0x1122334455667788, jump);
    if (jump[0] != 0x58000050 || jump[1] != 0xD61F0200 || jump[2] != 0x55667788 || jump[3] != 0x11223344) {
        std::printf("EncodeAbsoluteJump is wrong\n");
        ok = false;
    }
    std::printf("instruction helpers: %s\n", ok ? "ok" : "FAILED");
    return ok;
}

bool CheckKeycodes()
{
    bool ok = true;
    // kVK_* codes and the HID usage ids the config's key names stand for.
    const int pairs[][2] = { { 0x0D, 26 }, { 0x00, 4 }, { 0x01, 22 }, { 0x02, 7 }, { 0x39, 57 },
        { 0x35, 41 }, { 0x31, 44 }, { 0x30, 43 }, { 0x24, 40 }, { 0x33, 42 }, { 0x75, 76 }, { 0x7E, 82 },
        { 0x7D, 81 }, { 0x7B, 80 }, { 0x7C, 79 }, { 0x7A, 58 }, { 0x6F, 69 }, { 0x5A, 111 }, { 0x1D, 39 },
        { 0x12, 30 }, { 0x41, 99 }, { 0x47, 83 }, { 0x3B, 224 }, { 0x38, 225 }, { 0x3A, 226 },
        { 0x37, 227 }, { 0x3E, 228 }, { 0x3C, 229 }, { 0x3D, 230 }, { 0x36, 231 } };
    for (const auto& pair : pairs) {
        if (keycodes::HidFromVirtualKey(static_cast<unsigned short>(pair[0])) != pair[1]) {
            std::printf("HidFromVirtualKey(%#x) is %d, not %d\n", pair[0],
                keycodes::HidFromVirtualKey(static_cast<unsigned short>(pair[0])), pair[1]);
            ok = false;
        }
    }
    // Every key the table knows maps back to itself: no two virtual keys share a HID usage id.
    for (int key = 0; key < 128; ++key) {
        const int hid = keycodes::HidFromVirtualKey(static_cast<unsigned short>(key));
        if (hid && keycodes::VirtualKeyFromHid(hid) != key) {
            std::printf("VirtualKeyFromHid(%d) is %#x, not %#x\n", hid, keycodes::VirtualKeyFromHid(hid), key);
            ok = false;
        }
    }
    // The key names the macOS config lists exist on a Mac keyboard.
    for (const char* name : { "key:capslock", "key:w", "key:0", "key:f20", "key:backspace", "key:delete",
             "key:insert", "key:numlock", "key:kp_period", "key:kp_9", "key:lgui", "key:rgui", "key:ralt",
             "key:`", "key:\\", "key:pagedown" }) {
        hotkeys::Key key{};
        if (!hotkeys::ParseKey(name, key) || keycodes::VirtualKeyFromHid(key.code) < 0) {
            std::printf("%s has no Mac key\n", name);
            ok = false;
        }
    }
    // NSEvent modifier flags: the side comes from the device bits, the left key without them.
    struct Flags {
        uint64_t flags;
        const char* down;  // left Control, Shift, Option, Command, then the right ones
    };
    const Flags flags[] = { { 0, "00000000" }, { 0x20002, "01000000" }, { 0x20004, "00000100" },
        { 0x20000, "01000000" }, { 0x20006, "01000100" }, { 0x80040, "00000010" }, { 0x80020, "00100000" },
        { 0x42000, "00001000" }, { 0x40001, "10000000" }, { 0x100010, "00000001" }, { 0x100008, "00010000" },
        { 0x10000, "00000000" }, { 0x1, "00000000" } };
    for (const Flags& f : flags) {
        bool down[8];
        keycodes::ModifiersDown(f.flags, down);
        char got[9] = {};
        for (int i = 0; i < 8; ++i) {
            got[i] = down[i] ? '1' : '0';
        }
        if (std::strcmp(got, f.down) != 0) {
            std::printf("ModifiersDown(%#" PRIx64 ") is %s, not %s\n", f.flags, got, f.down);
            ok = false;
        }
    }
    std::printf("key codes: %s\n", ok ? "ok" : "FAILED");
    return ok;
}

// Writes `text` to a temporary file and reads it as the config.
bool ParseConfig(const char* text, Settings& settings)
{
    const char* tmp = std::getenv("TMPDIR");
    std::string path = std::string(tmp && *tmp ? tmp : "/tmp") + "/symtest-XXXXXX";
    const int fd = mkstemp(path.data());
    if (fd < 0) {
        return false;
    }
    const bool written = write(fd, text, std::strlen(text)) == static_cast<ssize_t>(std::strlen(text));
    close(fd);
    const bool parsed = written && ParseSettingsFile(path, settings);
    unlink(path.c_str());
    return parsed;
}

bool CheckHotkeys()
{
    bool ok = true;
    auto expect = [&](bool condition, const char* what) {
        if (!condition) {
            std::printf("hotkeys: %s\n", what);
            ok = false;
        }
    };
    using hotkeys::Key;
    const Key w{ Key::Scancode, 26 }, s{ Key::Scancode, 22 }, caps{ Key::Scancode, hotkeys::kCapsLock };
    const Key lshift{ Key::Scancode, hotkeys::kLShift }, lalt{ Key::Scancode, hotkeys::kLAlt };

    hotkeys::SetBindings(Settings{});
    expect(hotkeys::Matches(hotkeys::ToggleMovementMode, caps), "Caps Lock doesn't toggle the mode");
    expect(hotkeys::Matches(hotkeys::Forward, w), "W isn't Forward");
    expect(!hotkeys::Matches(hotkeys::ToggleAutoforward, w), "W alone toggles autoforward");
    hotkeys::SetDown(lshift, true);
    expect(hotkeys::Matches(hotkeys::ToggleAutoforward, w), "Shift+W doesn't toggle autoforward");
    expect(!hotkeys::Matches(hotkeys::Forward, w), "Shift+W is Forward");
    hotkeys::SetDown(lshift, false);

    hotkeys::SetMovementKeys({ "key:e" }, { "key:d" });
    expect(hotkeys::Matches(hotkeys::Forward, Key{ Key::Scancode, 8 }), "E isn't Forward after SetMovementKeys");
    expect(!hotkeys::Matches(hotkeys::Backward, s), "S is still Backward after SetMovementKeys");
    hotkeys::SetMovementKeys({ "key:w", "key:up" }, { "key:s", "key:down" });

    // A config with a hold key, a mouse button and a decimal WalkSpeed.
    Settings settings;
    const bool parsed = ParseConfig("[ModHotkeys]\n"
                                    "ToggleWalkspeed = \"key:r\" # comment\n"
                                    "HoldWalkspeed = 'key:lalt'\n"
                                    "HoldMovementMode = \"mouse:x1, ctrl+key:F5\"\n"
                                    "[Walking]\n"
                                    "WalkSpeed = 0.75\n"
                                    "IsWalkingDefault = true\n"
                                    "IsCharacterMovementDefault = false\n",
        settings);
    expect(parsed, "the config wasn't read");
    expect(settings.walk_speed == 0.75f, "WalkSpeed isn't 0.75");
    expect(settings.is_walking_default && !settings.is_character_movement_default, "the booleans weren't read");
    expect(settings.toggle_movement_mode == "key:capslock", "a missing setting lost its default");
    hotkeys::SetBindings(settings);
    expect(hotkeys::Matches(hotkeys::ToggleWalkspeed, Key{ Key::Scancode, 21 }), "R doesn't toggle walking");
    expect(hotkeys::Matches(hotkeys::HoldWalkspeed, lalt), "Left Option doesn't hold walking");
    expect(hotkeys::Matches(hotkeys::HoldMovementMode, Key{ Key::MouseButton, 4 }), "mouse:x1 isn't bound");
    expect(!hotkeys::Matches(hotkeys::HoldMovementMode, Key{ Key::Scancode, 62 }), "F5 matches without Ctrl");
    hotkeys::SetDown(Key{ Key::Scancode, hotkeys::kRCtrl }, true);
    expect(hotkeys::Matches(hotkeys::HoldMovementMode, Key{ Key::Scancode, 62 }), "Right Ctrl+F5 doesn't match");
    hotkeys::SetDown(Key{ Key::Scancode, hotkeys::kRCtrl }, false);

    // WalkSpeed reads the same in a locale with a decimal comma.
    for (const char* locale : { "de_DE.UTF-8", "ru_RU.UTF-8", "fr_FR.UTF-8", "de_DE", "ru_RU" }) {
        if (std::setlocale(LC_NUMERIC, locale)) {
            Settings comma;
            expect(ParseConfig("WalkSpeed = 0.3\n", comma) && comma.walk_speed == 0.3f,
                "WalkSpeed 0.3 is read differently in a decimal-comma locale");
            std::setlocale(LC_NUMERIC, "C");
            break;
        }
    }
    hotkeys::SetBindings(Settings{});
    std::printf("hotkeys and config: %s\n", ok ? "ok" : "FAILED");
    return ok;
}

// The code of the function at `start`, up to the next symbol, read from the file.
std::vector<uint32_t> CodeOf(const macho::Image& image, const uint8_t* slice, size_t size, uint64_t start)
{
    const uint64_t end = macho::NextSymbolAddress(image, start);
    const uint64_t at = macho::FileOffset(image.header, start);
    std::vector<uint32_t> code;
    if (start && at && end > start && end - start <= 0x10000 && at + (end - start) <= size) {
        code.resize((end - start) / 4);
        std::memcpy(code.data(), slice + at, code.size() * 4);
    }
    return code;
}

bool CheckSlice(const std::vector<uint8_t>& file, uint32_t cputype, const char* arch)
{
    size_t size = 0;
    const uint8_t* slice = macho::FindSlice(file.data(), file.size(), cputype, size);
    macho::Image image;
    if (!slice || !macho::ImageFromFile(slice, size, image)) {
        std::printf("%s: no slice or no symbol table\n", arch);
        return false;
    }
    uintptr_t addresses[symbols::Count];
    const size_t found = macho::FindSymbols(image, symbols::kNames, addresses, symbols::Count);
    std::printf("%s: %u symbols, found %zu of %d\n", arch, image.symbol_count, found, int(symbols::Count));
    for (int i = 0; i < symbols::Count; ++i) {
        std::printf("  %#12" PRIxPTR "  %s\n", addresses[i], symbols::kNames[i]);
    }
    bool ok = found == symbols::Count;

    const uint64_t slot = addresses[symbols::CameraSystemVtable] + symbols::kVtableAddressPoint +
        symbols::kCameraOnInputEventSlot;
    const uint64_t slot_offset = macho::FileOffset(image.header, slot);
    uint64_t target = 0;
    if (slot_offset && slot_offset + sizeof target <= size) {
        std::memcpy(&target, slice + slot_offset, sizeof target);
    }
    const bool slot_ok = target && target == addresses[symbols::CameraSystemOnInputEvent];
    std::printf("  CameraSystem vtable slot %#zx -> %#" PRIx64 ": %s\n", symbols::kCameraOnInputEventSlot,
        target, slot_ok ? "OnInputEvent" : "MISMATCH");
    ok = ok && slot_ok;

    // The text field check and mouselook hook gui::GameUI::Update and OnIsGameMenuOpenChanged in the
    // GameUI vtable, found by their targets.
    for (int index : { int(symbols::GameUIUpdate), int(symbols::OnIsGameMenuOpenChanged) }) {
        int found_slot = -1;
        for (int i = 0; i < int(symbols::kGameUIVtableSlots) && found_slot < 0; ++i) {
            const uint64_t at = macho::FileOffset(image.header,
                addresses[symbols::GameUIVtable] + symbols::kVtableAddressPoint + 8 * uint64_t(i));
            uint64_t value = 0;
            if (at && at + sizeof value <= size) {
                std::memcpy(&value, slice + at, sizeof value);
            }
            if (value && value == addresses[index]) {
                found_slot = i;
            }
        }
        if (found_slot >= 0) {
            std::printf("  GameUI vtable slot %#x -> %s\n", 8 * found_slot, symbols::kNames[index]);
        } else {
            std::printf("  GameUI vtable: %s NOT FOUND\n", symbols::kNames[index]);
        }
        ok = ok && found_slot >= 0;
    }

    if (cputype == macho::kCpuArm64) {
        // Inline hooks move the first 4 instructions of GetRotatedInput and of the forced cursor
        // functions, so those must not be PC-relative.
        for (int index : { int(symbols::GetRotatedInput), int(symbols::AddForcedMouseCursor),
                 int(symbols::RemoveForcedMouseCursor), int(symbols::RecordDialogStart),
                 int(symbols::RecordDialogEnd), int(symbols::IsFading), int(symbols::OnSelectedCharacterChanged),
                 int(symbols::CalcCamTargetDest),
                 int(symbols::GetInputValue), int(symbols::OnIsGameMenuOpenChanged) }) {
            const uint64_t at = macho::FileOffset(image.header, addresses[index]);
            uint32_t words[4] = {};
            if (at && at + sizeof words <= size) {
                std::memcpy(words, slice + at, sizeof words);
            }
            bool movable = at != 0;
            for (uint32_t word : words) {
                movable = movable && !arm64::IsPcRelative(word);
            }
            std::printf("  prologue %08x %08x %08x %08x %-11s %s\n", words[0], words[1], words[2],
                words[3], movable ? "movable" : "PC-relative", symbols::kNames[index]);
            if (index == symbols::GetRotatedInput || index == symbols::AddForcedMouseCursor ||
                index == symbols::RemoveForcedMouseCursor || index == symbols::RecordDialogStart ||
                index == symbols::RecordDialogEnd || index == symbols::IsFading ||
                index == symbols::OnSelectedCharacterChanged || index == symbols::CalcCamTargetDest) {
                ok = ok && movable;
            }
        }

        // The WASD unlock: the flag SetKeyboardMode clears, and the check of it in CanExecute.
        const uint64_t setter = addresses[symbols::SetKeyboardMode];
        const std::vector<uint32_t> setter_code = CodeOf(image, slice, size, setter);
        const uint64_t flag = patterns::FindControllerModeFlag(setter_code.data(), setter_code.size(), setter);
        const uint64_t can_execute = addresses[symbols::MoveControllerCanExecute];
        const std::vector<uint32_t> check_code = CodeOf(image, slice, size, can_execute);
        const long check = flag ? patterns::FindControllerModeCheck(check_code.data(), check_code.size(), can_execute, flag) : -1;
        std::printf("  WASD unlock: controller-mode flag %#" PRIx64 ", check %s", flag, check >= 0 ? "at " : "NOT FOUND");
        if (check >= 0) {
            std::printf("%#" PRIx64 " (%08x)", can_execute + 4 * uint64_t(check), check_code[size_t(check)]);
        }
        std::printf("\n");
        ok = ok && flag && check >= 0;
    }
    return ok;
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <path to the \"Baldur's Gate 3\" executable>\n", argv[0]);
        return 2;
    }
    std::ifstream in(argv[1], std::ios::binary | std::ios::ate);
    if (!in) {
        std::fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }
    std::vector<uint8_t> file(static_cast<size_t>(in.tellg()));
    in.seekg(0);
    in.read(reinterpret_cast<char*>(file.data()), static_cast<std::streamsize>(file.size()));

    bool ok = CheckEncodings();
    ok = CheckKeycodes() && ok;
    ok = CheckHotkeys() && ok;
    ok = CheckSlice(file, macho::kCpuArm64, "arm64") && ok;
    ok = CheckSlice(file, macho::kCpuX86_64, "x86_64") && ok;
    std::printf("%s\n", ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
}
