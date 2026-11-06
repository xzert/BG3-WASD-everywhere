#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace wasd::mem {

struct Segment {
    uintptr_t start = 0;
    size_t size = 0;

    bool Contains(uintptr_t address) const { return address >= start && address - start < size; }
};

struct MainExecutable {
    uintptr_t load_bias = 0;  // runtime address minus the address in the ELF file
    Segment code;             // the executable PT_LOAD segment
    Segment data[4];          // writable PT_LOAD segments
    int data_count = 0;
    Segment relro;            // part of the data made read-only after relocation (PT_GNU_RELRO)
};

bool FindMainExecutable(MainExecutable& exe);

// Overwrites code, making its pages writable only for the copy.
bool WriteCode(uintptr_t address, const void* bytes, size_t size);

// Creates `jmp qword ptr [rip+0] ; dq destination` within rel32 reach of `near`. The library is
// loaded too far from the game for a `call rel32` to reach it directly.
void* MakeJumpStub(uintptr_t near, const void* destination);

// Points the `call rel32` at `site` to `stub`. Returns the previous target, or 0 on failure.
uintptr_t RedirectCall(uintptr_t site, const void* stub);

// Replaces the 6-byte `call qword ptr [rax+disp32]` at `site` with `call stub ; nop`.
bool ReplaceVirtualCall(uintptr_t site, const void* stub);

// 8-byte aligned slots in the game's data segments that hold `value`, e.g. vtable entries.
std::vector<uintptr_t> FindPointerSlots(const MainExecutable& exe, uintptr_t value);

// Writes a pointer into the game's data, restoring read-only protection inside RELRO.
bool WritePointer(const MainExecutable& exe, uintptr_t slot, uintptr_t value);

}  // namespace wasd::mem
