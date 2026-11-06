#include "memory.hpp"

#include <link.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstring>
#include <initializer_list>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

namespace wasd::mem {
namespace {

constexpr int64_t kRel32Reach = 0x7FFF0000;

int OnLoadedObject(dl_phdr_info* info, size_t, void* data)
{
    // The first object dl_iterate_phdr reports is the main program.
    auto& exe = *static_cast<MainExecutable*>(data);
    exe.load_bias = info->dlpi_addr;
    for (int i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr)& ph = info->dlpi_phdr[i];
        const Segment segment{ info->dlpi_addr + ph.p_vaddr, ph.p_memsz };
        if (ph.p_type == PT_GNU_RELRO) {
            exe.relro = segment;
        }
        if (ph.p_type != PT_LOAD) {
            continue;
        }
        if ((ph.p_flags & PF_X) && segment.size > exe.code.size) {
            exe.code = segment;
        } else if ((ph.p_flags & PF_W) && exe.data_count < 4) {
            exe.data[exe.data_count++] = segment;
        } else if (!(ph.p_flags & (PF_X | PF_W)) && segment.size > exe.rodata.size) {
            exe.rodata = segment;
        }
    }
    return 1;  // stop after the main program
}

bool Protect(uintptr_t address, size_t size, int protection)
{
    const auto page = static_cast<uintptr_t>(sysconf(_SC_PAGESIZE));
    const uintptr_t begin = address & ~(page - 1);
    const uintptr_t end = (address + size + page - 1) & ~(page - 1);
    return mprotect(reinterpret_cast<void*>(begin), end - begin, protection) == 0;
}

bool FitsRel32(uintptr_t from, uintptr_t to)
{
    const int64_t distance = static_cast<int64_t>(to - from);
    return distance >= -kRel32Reach && distance <= kRel32Reach;
}

uint8_t* AllocateNear(uintptr_t near, size_t size)
{
    constexpr uintptr_t kStep = 1 << 20;
    const uintptr_t origin = near & ~(kStep - 1);
    for (uintptr_t delta = kStep; delta < (uintptr_t{ 1 } << 30); delta += kStep) {
        for (const uintptr_t hint : { origin - delta, origin + delta }) {
            void* p = mmap(reinterpret_cast<void*>(hint), size, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
            if (p == MAP_FAILED) {
                continue;
            }
            if (FitsRel32(near, reinterpret_cast<uintptr_t>(p))) {
                return static_cast<uint8_t*>(p);
            }
            munmap(p, size);  // an old kernel treated the address as a plain hint
        }
    }
    return nullptr;
}

}  // namespace

bool FindMainExecutable(MainExecutable& exe)
{
    exe = {};
    dl_iterate_phdr(OnLoadedObject, &exe);
    return exe.code.size != 0;
}

bool WriteCode(uintptr_t address, const void* bytes, size_t size)
{
    if (!Protect(address, size, PROT_READ | PROT_WRITE | PROT_EXEC)) {
        return false;
    }
    std::memcpy(reinterpret_cast<void*>(address), bytes, size);
    __builtin___clear_cache(reinterpret_cast<char*>(address), reinterpret_cast<char*>(address + size));
    return Protect(address, size, PROT_READ | PROT_EXEC);
}

void* MakeJumpStub(uintptr_t near, const void* destination)
{
    const auto page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    uint8_t* stub = AllocateNear(near, page);
    if (!stub) {
        return nullptr;
    }
    static constexpr uint8_t kJmpRipIndirect[6] = { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
    std::memcpy(stub, kJmpRipIndirect, sizeof kJmpRipIndirect);
    std::memcpy(stub + sizeof kJmpRipIndirect, &destination, sizeof destination);
    if (mprotect(stub, page, PROT_READ | PROT_EXEC) != 0) {
        munmap(stub, page);
        return nullptr;
    }
    return stub;
}

uintptr_t RedirectCall(uintptr_t site, const void* stub)
{
    const auto* code = reinterpret_cast<const uint8_t*>(site);
    if (code[0] != 0xE8) {
        return 0;
    }
    int32_t rel;
    std::memcpy(&rel, code + 1, sizeof rel);
    const uintptr_t original = site + 5 + static_cast<int64_t>(rel);

    const auto target = reinterpret_cast<uintptr_t>(stub);
    if (!FitsRel32(site + 5, target)) {
        return 0;
    }
    const auto new_rel = static_cast<int32_t>(static_cast<int64_t>(target - (site + 5)));
    if (!WriteCode(site + 1, &new_rel, sizeof new_rel)) {
        return 0;
    }
    return original;
}

bool WriteCodeAtomic16(uintptr_t address, uint16_t value)
{
    if ((address & 63) == 63 || !Protect(address, sizeof value, PROT_READ | PROT_WRITE | PROT_EXEC)) {
        return false;
    }
    *reinterpret_cast<volatile uint16_t*>(address) = value;
    __builtin___clear_cache(reinterpret_cast<char*>(address), reinterpret_cast<char*>(address + sizeof value));
    return Protect(address, sizeof value, PROT_READ | PROT_EXEC);
}

bool UnhookEntry(uintptr_t function, const uint8_t* expected, size_t length)
{
    return WriteCode(function, expected, length);
}

bool HookEntry(uintptr_t function, const uint8_t* expected, size_t length, const void* hook, void** original)
{
    if (length < 5 || length > 32 || std::memcmp(reinterpret_cast<const void*>(function), expected, length) != 0) {
        return false;
    }
    const auto page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    uint8_t* block = AllocateNear(function, page);
    if (!block) {
        return false;
    }
    // Trampoline: the moved instructions, then `jmp function+length`.
    uint8_t* trampoline = block;
    std::memcpy(trampoline, expected, length);
    trampoline[length] = 0xE9;
    const auto back = static_cast<int32_t>(
        static_cast<int64_t>(function + length - reinterpret_cast<uintptr_t>(trampoline + length + 5)));
    std::memcpy(trampoline + length + 1, &back, sizeof back);
    // Stub: `jmp qword ptr [rip+0] ; dq hook`, within rel32 reach of the function.
    uint8_t* stub = block + 64;
    static constexpr uint8_t kJmpRipIndirect[6] = { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
    std::memcpy(stub, kJmpRipIndirect, sizeof kJmpRipIndirect);
    std::memcpy(stub + sizeof kJmpRipIndirect, &hook, sizeof hook);
    if (mprotect(block, page, PROT_READ | PROT_EXEC) != 0) {
        munmap(block, page);
        return false;
    }

    *original = trampoline;
    uint8_t patch[32];
    std::memset(patch, 0xCC, length);  // int3 after the jump; never reached
    patch[0] = 0xE9;
    const auto to_stub = static_cast<int32_t>(static_cast<int64_t>(reinterpret_cast<uintptr_t>(stub) - (function + 5)));
    std::memcpy(patch + 1, &to_stub, sizeof to_stub);
    return WriteCode(function, patch, length);
}

bool ReplaceVirtualCall(uintptr_t site, const void* stub)
{
    const auto* code = reinterpret_cast<const uint8_t*>(site);
    const auto target = reinterpret_cast<uintptr_t>(stub);
    if (code[0] != 0xFF || code[1] != 0x90 || !FitsRel32(site + 5, target)) {
        return false;
    }
    uint8_t patch[6] = { 0xE8, 0, 0, 0, 0, 0x90 };
    const auto rel = static_cast<int32_t>(static_cast<int64_t>(target - (site + 5)));
    std::memcpy(patch + 1, &rel, sizeof rel);
    return WriteCode(site, patch, sizeof patch);
}

std::vector<uintptr_t> FindPointerSlots(const MainExecutable& exe, uintptr_t value)
{
    std::vector<uintptr_t> slots;
    for (int i = 0; i < exe.data_count; ++i) {
        const Segment& segment = exe.data[i];
        const uintptr_t end = segment.start + segment.size;
        for (uintptr_t p = (segment.start + 7) & ~uintptr_t{ 7 }; p + 8 <= end; p += 8) {
            if (*reinterpret_cast<const uintptr_t*>(p) == value) {
                slots.push_back(p);
            }
        }
    }
    return slots;
}

bool WritePointer(const MainExecutable& exe, uintptr_t slot, uintptr_t value)
{
    if (!Protect(slot, sizeof value, PROT_READ | PROT_WRITE)) {
        return false;
    }
    std::memcpy(reinterpret_cast<void*>(slot), &value, sizeof value);
    return !exe.relro.Contains(slot) || Protect(slot, sizeof value, PROT_READ);
}

}  // namespace wasd::mem
