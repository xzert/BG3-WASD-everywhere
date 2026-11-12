#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// AArch64 instruction helpers for inline hooks and code patterns. Plain integer code, so the host
// test checks it too.
namespace wasd::arm64 {

inline constexpr uint32_t kNop = 0xD503201F;

// Whether the instruction depends on its own address: branches, CBZ/TBZ, ADR/ADRP and literal
// loads. Such instructions can't be copied into a trampoline as they are.
bool IsPcRelative(uint32_t instruction);

// `ldr x16, #8 ; br x16 ; .quad target`: an absolute jump in 16 bytes that only uses x16, the
// register the calling convention leaves free for this at function entry.
void EncodeAbsoluteJump(uint64_t target, uint32_t out[4]);

// adrp xd, label: the 4 KiB page it loads, for an instruction at `pc`.
bool DecodeAdrp(uint32_t instruction, uint64_t pc, int& rd, uint64_t& page);

// ldrb/strb wt, [xn, #offset], unsigned offset form. rt 31 is wzr.
bool DecodeByteAccess(uint32_t instruction, bool& store, int& rt, int& rn, uint32_t& offset);

// cbz/cbnz: the branch target, for an instruction at `pc`.
bool DecodeCompareBranch(uint32_t instruction, uint64_t pc, bool& nonzero, int& rt, uint64_t& target);

// The target of a branch at `pc`: B, BL, B.cond, CBZ/CBNZ or TBZ/TBNZ.
bool DecodeBranch(uint32_t instruction, uint64_t pc, uint64_t& target);

// Whether any branch in code[0..count) (starting at `pc`) lands strictly inside
// [first, first + size): code there can't be replaced by a jump.
bool BranchesInto(const uint32_t* code, size_t count, uint64_t pc, uint64_t first, size_t size);

// A byte loaded or stored through `adrp xn, page` and `ldrb/strb wt, [xn, #offset]`.
struct ByteAccess {
    size_t index;  // of the ldrb/strb in the code
    bool store;
    int rt;
    uint64_t address;
};

// Every such access in code[0..count), which starts at address `pc`.
std::vector<ByteAccess> FindByteAccesses(const uint32_t* code, size_t count, uint64_t pc);

}  // namespace wasd::arm64
