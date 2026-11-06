// Host check of the macOS probe's Mach-O and AArch64 code against the game's macOS executable. It
// finds the probe's symbols in both slices the way the library does in memory, checks the
// ecl::CameraSystem vtable slot, and checks that the code probe's target can be hooked.
// Builds on Linux and macOS.
//
// Usage: symtest <path to the "Baldur's Gate 3" executable>

#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

#include "arm64.hpp"
#include "macho.hpp"
#include "patterns.hpp"
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

    if (cputype == macho::kCpuArm64) {
        // The code probe hooks GetRotatedInput, so its first 4 instructions must be movable.
        for (int index : { int(symbols::GetRotatedInput), int(symbols::GetInputValue),
                 int(symbols::OnIsGameMenuOpenChanged) }) {
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
            if (index == symbols::GetRotatedInput) {
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
    ok = CheckSlice(file, macho::kCpuArm64, "arm64") && ok;
    ok = CheckSlice(file, macho::kCpuX86_64, "x86_64") && ok;
    std::printf("%s\n", ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
}
