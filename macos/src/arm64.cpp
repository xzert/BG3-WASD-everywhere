#include "arm64.hpp"

namespace wasd::arm64 {

bool IsPcRelative(uint32_t instruction)
{
    return (instruction & 0x7C000000) == 0x14000000      // B, BL
        || (instruction & 0xFF000000) == 0x54000000      // B.cond, BC.cond
        || (instruction & 0x7E000000) == 0x34000000      // CBZ, CBNZ
        || (instruction & 0x7E000000) == 0x36000000      // TBZ, TBNZ
        || (instruction & 0x1F000000) == 0x10000000      // ADR, ADRP
        || (instruction & 0x3B000000) == 0x18000000;     // LDR, LDRSW, PRFM (literal)
}

void EncodeAbsoluteJump(uint64_t target, uint32_t out[4])
{
    out[0] = 0x58000050;  // ldr x16, #8
    out[1] = 0xD61F0200;  // br x16
    out[2] = uint32_t(target);
    out[3] = uint32_t(target >> 32);
}

bool DecodeAdrp(uint32_t instruction, uint64_t pc, int& rd, uint64_t& page)
{
    if ((instruction & 0x9F000000) != 0x90000000) {
        return false;
    }
    int64_t pages = int64_t((instruction >> 29) & 0x3) | (int64_t((instruction >> 5) & 0x7FFFF) << 2);
    if (pages & (int64_t(1) << 20)) {
        pages -= int64_t(1) << 21;
    }
    rd = int(instruction & 0x1F);
    page = (pc & ~uint64_t(0xFFF)) + uint64_t(pages * 4096);
    return true;
}

bool DecodeByteAccess(uint32_t instruction, bool& store, int& rt, int& rn, uint32_t& offset)
{
    const uint32_t opcode = instruction & 0xFFC00000;
    if (opcode != 0x39400000 && opcode != 0x39000000) {  // LDRB, STRB (unsigned offset)
        return false;
    }
    store = opcode == 0x39000000;
    rt = int(instruction & 0x1F);
    rn = int((instruction >> 5) & 0x1F);
    offset = (instruction >> 10) & 0xFFF;
    return true;
}

bool DecodeCompareBranch(uint32_t instruction, uint64_t pc, bool& nonzero, int& rt, uint64_t& target)
{
    if ((instruction & 0x7E000000) != 0x34000000) {
        return false;
    }
    int64_t words = int64_t((instruction >> 5) & 0x7FFFF);
    if (words & (int64_t(1) << 18)) {
        words -= int64_t(1) << 19;
    }
    nonzero = (instruction >> 24) & 1;
    rt = int(instruction & 0x1F);
    target = pc + uint64_t(words * 4);
    return true;
}

bool DecodeBranch(uint32_t instruction, uint64_t pc, uint64_t& target)
{
    // Sign-extends the `bits`-bit field at `shift` and scales it to bytes.
    auto offset = [instruction](int shift, int bits) {
        int64_t words = int64_t((instruction >> shift) & ((uint32_t(1) << bits) - 1));
        if (words & (int64_t(1) << (bits - 1))) {
            words -= int64_t(1) << bits;
        }
        return words * 4;
    };
    if ((instruction & 0x7C000000) == 0x14000000) {  // B, BL
        target = pc + uint64_t(offset(0, 26));
    } else if ((instruction & 0xFF000010) == 0x54000000 || (instruction & 0x7E000000) == 0x34000000) {
        target = pc + uint64_t(offset(5, 19));  // B.cond, CBZ, CBNZ
    } else if ((instruction & 0x7E000000) == 0x36000000) {  // TBZ, TBNZ
        target = pc + uint64_t(offset(5, 14));
    } else {
        return false;
    }
    return true;
}

bool BranchesInto(const uint32_t* code, size_t count, uint64_t pc, uint64_t first, size_t size)
{
    for (size_t i = 0; i < count; ++i) {
        uint64_t target = 0;
        if (DecodeBranch(code[i], pc + 4 * i, target) && target > first && target < first + size) {
            return true;
        }
    }
    return false;
}

std::vector<ByteAccess> FindByteAccesses(const uint32_t* code, size_t count, uint64_t pc)
{
    std::vector<ByteAccess> found;
    for (size_t i = 0; i < count; ++i) {
        bool store = false;
        int rt = 0, rn = 0;
        uint32_t offset = 0;
        if (!DecodeByteAccess(code[i], store, rt, rn, offset)) {
            continue;
        }
        // The adrp that set the base register is usually a few instructions earlier.
        for (size_t back = 1; back <= 8 && back <= i; ++back) {
            int rd = 0;
            uint64_t page = 0;
            if (DecodeAdrp(code[i - back], pc + 4 * (i - back), rd, page)) {
                if (rd == rn) {
                    found.push_back({ i, store, rt, page + offset });
                    break;
                }
            }
        }
    }
    return found;
}

}  // namespace wasd::arm64
