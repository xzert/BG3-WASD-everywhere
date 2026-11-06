#include "patterns.hpp"

#include "arm64.hpp"

namespace wasd::patterns {

uint64_t FindControllerModeFlag(const uint32_t* code, size_t count, uint64_t pc)
{
    constexpr int kZeroRegister = 31;
    uint64_t flag = 0;
    int stores = 0;
    for (const arm64::ByteAccess& access : arm64::FindByteAccesses(code, count, pc)) {
        if (access.store && access.rt == kZeroRegister) {
            flag = access.address;
            ++stores;
        }
    }
    return stores == 1 ? flag : 0;
}

long FindControllerModeCheck(const uint32_t* code, size_t count, uint64_t pc, uint64_t flag)
{
    long check = -1;
    int checks = 0;
    for (const arm64::ByteAccess& access : arm64::FindByteAccesses(code, count, pc)) {
        if (access.store || access.address != flag || access.index + 1 >= count) {
            continue;
        }
        bool nonzero = false;
        int rt = 0;
        uint64_t target = 0;
        const size_t next = access.index + 1;
        if (arm64::DecodeCompareBranch(code[next], pc + 4 * next, nonzero, rt, target) && !nonzero &&
            rt == access.rt) {
            check = long(next);
            ++checks;
        }
    }
    return checks == 1 ? check : -1;
}

}  // namespace wasd::patterns
