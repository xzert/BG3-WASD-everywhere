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

namespace {

constexpr uint32_t kMovX0X26 = 0xAA1A03E0;        // mov x0, x26
constexpr uint32_t kMovW1Zero = 0x52800001;       // mov w1, #0
constexpr uint32_t kLoadCameraPitch = 0xBD416741;  // ldr s1, [x26, #0x164]
constexpr uint32_t kZoomScale = 0x1E220821;       // fmul s1, s1, s2
constexpr uint32_t kZoomApply = 0x1E200820;       // fmul s0, s1, s0
constexpr uint32_t kStoreZoomSpeed = 0xBD00A6C0;  // str s0, [x22, #0xA4]
constexpr uint32_t kMoveW8ToS2 = 0x1E270102;      // fmov s2, w8
constexpr uint32_t kStoreAngleSpeed = 0xBD009EC0; // str s0, [x22, #0x9C]
constexpr uint32_t kMovX24X21 = 0xAA1503F8;       // mov x24, x21
constexpr uint32_t kMovX2X22 = 0xAA1603E2;        // mov x2, x22
constexpr uint32_t kMovX22X25 = 0xAA1903F6;       // mov x22, x25
constexpr uint32_t kEorW0One = 0x52000000;        // eor wN, w0, #1, with N in the low 5 bits
constexpr size_t kWindow = 16;                    // the bytes the jump replaces

// Only the first instruction of a replaced window may be a branch target.
bool Replaceable(const uint32_t* code, size_t count, uint64_t pc, size_t index)
{
    return index + kWindow / 4 <= count && !arm64::BranchesInto(code, count, pc, pc + 4 * index, kWindow);
}

}  // namespace

bool FindPitchUpdate(const uint32_t* code, size_t count, uint64_t pc, uint64_t pitch_function, PitchUpdate& out)
{
    int found = 0;
    for (size_t i = 2; i + 1 < count; ++i) {
        uint64_t target = 0;
        if ((code[i] & 0xFC000000) == 0x94000000 && arm64::DecodeBranch(code[i], pc + 4 * i, target) &&
            target == pitch_function && code[i - 2] == kMovX0X26 && code[i - 1] == kMovW1Zero &&
            code[i + 1] == kLoadCameraPitch) {
            out.index = long(i - 2);
            ++found;
        }
    }
    if (found != 1 || !Replaceable(code, count, pc, size_t(out.index))) {
        return false;
    }

    // The definition, picked up to 24 instructions before the call.
    const size_t call = size_t(out.index) + 2;
    const size_t first = call > 24 ? call - 24 : 0;
    int settings_register = -1;
    size_t settings_index = 0;
    for (size_t i = first; i + 1 < call; ++i) {
        int rd = 0;
        uint64_t page = 0;
        const uint32_t load = code[i + 1];
        // adrp xN ; ldr xN, [xN, #offset] (64-bit, unsigned offset)
        if (arm64::DecodeAdrp(code[i], pc + 4 * i, rd, page) && (load & 0xFFC00000) == 0xF9400000 &&
            int(load & 0x1F) == rd && int((load >> 5) & 0x1F) == rd) {
            settings_register = rd;
            settings_index = i + 2;
            out.settings_pointer = page + ((load >> 10) & 0xFFF) * 8;
            out.override_flag = 0;
            for (size_t j = i + 2; j < call; ++j) {
                if ((code[j] & 0xFFE00000) == 0x52800000) {  // movz wN, #imm16
                    out.override_flag = (code[j] >> 5) & 0xFFFF;
                    break;
                }
            }
        }
    }
    if (settings_register < 0 || !out.override_flag) {
        return false;
    }
    // add xD, x<settings>, #offset ; and the csel that picks exploration (eq: not combat) or combat.
    struct Add {
        int rd;
        uint32_t offset;
    };
    Add adds[4];
    int add_count = 0;
    int csel_n = -1, csel_m = -1, csel_cond = -1;
    for (size_t j = settings_index; j < call; ++j) {
        const uint32_t w = code[j];
        if ((w & 0xFFC00000) == 0x91000000 && int((w >> 5) & 0x1F) == settings_register && add_count < 4) {
            adds[add_count++] = { int(w & 0x1F), (w >> 10) & 0xFFF };
        } else if ((w & 0xFFE00C00) == 0x9A800000) {  // csel xD, xN, xM, cond
            csel_n = int((w >> 5) & 0x1F);
            csel_m = int((w >> 16) & 0x1F);
            csel_cond = int((w >> 12) & 0xF);
        }
    }
    if (add_count != 3 || (csel_cond != 0 && csel_cond != 1)) {  // eq or ne
        return false;
    }
    const int exploration_register = csel_cond == 0 ? csel_n : csel_m;
    const int combat_register = csel_cond == 0 ? csel_m : csel_n;
    for (int k = 0; k < add_count; ++k) {
        if (adds[k].rd == exploration_register) {
            out.exploration = adds[k].offset;
        } else if (adds[k].rd == combat_register) {
            out.combat = adds[k].offset;
        } else {
            out.override_definition = adds[k].offset;
        }
    }
    return out.exploration && out.combat && out.override_definition && out.exploration != out.combat;
}

long FindZoomSpeedStore(const uint32_t* code, size_t count, uint64_t pc, uint64_t& next)
{
    long found = -1;
    int matches = 0;
    for (size_t i = 0; i + 3 < count; ++i) {
        if (code[i] == kZoomScale && code[i + 1] == kZoomApply && code[i + 2] == kStoreZoomSpeed &&
            (code[i + 3] & 0xFC000000) == 0x14000000) {
            found = long(i);
            ++matches;
        }
    }
    if (matches != 1 || !Replaceable(code, count, pc, size_t(found)) ||
        !arm64::DecodeBranch(code[found + 3], pc + 4 * uint64_t(found + 3), next)) {
        return -1;
    }
    return found;
}

long FindRotationSpeedStore(const uint32_t* code, size_t count, uint64_t pc)
{
    long found = -1;
    int matches = 0;
    for (size_t i = 0; i + 3 < count; ++i) {
        if (code[i] == kMoveW8ToS2 && code[i + 1] == kZoomScale && code[i + 2] == kZoomApply &&
            code[i + 3] == kStoreAngleSpeed) {
            found = long(i);
            ++matches;
        }
    }
    return matches == 1 && Replaceable(code, count, pc, size_t(found)) ? found : -1;
}

long FindChaseTargetTest(const uint32_t* code, size_t count, uint64_t pc, uint64_t hit_test)
{
    long found = -1;
    int matches = 0;
    for (size_t i = 3; i + 1 < count; ++i) {
        uint64_t target = 0;
        if ((code[i] & 0xFC000000) == 0x94000000 && arm64::DecodeBranch(code[i], pc + 4 * i, target) &&
            target == hit_test && (code[i + 1] & 0xFFFFFFE0) == kEorW0One && code[i - 3] == kMovX24X21 &&
            code[i - 2] == kMovX2X22 && code[i - 1] == kMovX22X25) {
            found = long(i - 3);
            ++matches;
        }
    }
    return matches == 1 && Replaceable(code, count, pc, size_t(found)) ? found : -1;
}

}  // namespace wasd::patterns
