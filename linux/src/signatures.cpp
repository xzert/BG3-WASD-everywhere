#include "signatures.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <vector>

namespace wasd {
namespace {

// "80 3D ?? 00" -> { 0x80, 0x3D, -1, 0x00 }
std::vector<int> ParsePattern(const char* pattern)
{
    auto hex = [](char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; };
    std::vector<int> bytes;
    for (const char* p = pattern; *p;) {
        if (*p == ' ') {
            ++p;
        } else if (*p == '?') {
            bytes.push_back(-1);
            p += p[1] == '?' ? 2 : 1;
        } else {
            bytes.push_back(hex(p[0]) * 16 + hex(p[1]));
            p += 2;
        }
    }
    return bytes;
}

// Offsets of the first `limit` matches.
std::vector<size_t> FindPattern(const uint8_t* text, size_t size, const char* pattern, size_t limit = 2)
{
    const std::vector<int> pat = ParsePattern(pattern);
    std::vector<size_t> hits;
    size_t anchor = 0;  // first non-wildcard byte, used with memchr
    while (anchor < pat.size() && pat[anchor] < 0) {
        ++anchor;
    }
    if (anchor == pat.size() || pat.size() > size) {
        return hits;
    }

    const size_t last_start = size - pat.size();
    for (size_t pos = 0; pos <= last_start && hits.size() < limit;) {
        const void* found = std::memchr(text + pos + anchor, pat[anchor], last_start - pos + 1);
        if (!found) {
            break;
        }
        const size_t start = static_cast<const uint8_t*>(found) - text - anchor;
        bool match = true;
        for (size_t i = 0; i < pat.size() && match; ++i) {
            match = pat[i] < 0 || text[start + i] == pat[i];
        }
        if (match) {
            hits.push_back(start);
        }
        pos = start + 1;
    }
    return hits;
}

int32_t ReadRel32(const uint8_t* p)
{
    int32_t value;
    std::memcpy(&value, p, sizeof value);
    return value;
}

// Offset that a rel32 field at `field` points to, given the end of its instruction.
int64_t RelTarget(const uint8_t* text, size_t field, size_t instruction_end)
{
    return static_cast<int64_t>(instruction_end) + ReadRel32(text + field);
}

}  // namespace

bool FindGameAddresses(const uint8_t* text, size_t size, uintptr_t text_address,
    GameAddresses& out, std::string& error)
{
    // UpdateWasdMove2 skips keyboard movement unless the game is in controller mode. The check is
    // followed by reading the controller at +0x68 (r12 on Linux, r15 on Windows).
    const auto unlock = FindPattern(text, size, "80 3D ?? ?? ?? ?? 00 0F 84 ?? ?? ?? ?? 49 8B 44 24");
    if (unlock.size() != 1) {
        error = "WASD unlock signature matched " + std::to_string(unlock.size()) + " times, expected 1";
        return false;
    }
    out.wasd_unlock = text_address + unlock[0];
    out.is_in_controller_mode = text_address + RelTarget(text, unlock[0] + 2, unlock[0] + 7);

    // GetMovementInputWorldVec: `mov rdi, rbx ; mov esi, 0xA0 ; mov edx, ebp ; call GetInputValue`,
    // then the same for 0x9F, 0x9D and 0x9E with a `movaps` spill in between.
    const auto first = FindPattern(text, size, "48 89 DF BE A0 00 00 00 89 EA E8");
    if (first.size() != 1) {
        error = "GetInputValue signature matched " + std::to_string(first.size()) + " times, expected 1";
        return false;
    }
    const size_t first_call = first[0] + 10;
    const int64_t target = RelTarget(text, first_call + 1, first_call + 5);
    if (target < 0 || static_cast<size_t>(target) >= size) {
        error = "GetInputValue call target is outside the game code";
        return false;
    }
    out.get_input_value = text_address + target;

    static constexpr uint32_t kCommandIds[4] = { 0xA0, 0x9F, 0x9D, 0x9E };
    size_t found = 0;
    for (size_t p = first_call; p + 5 <= size && p < first_call + 0x60 && found < 4; ++p) {
        if (text[p] != 0xE8 || RelTarget(text, p + 1, p + 5) != target) {
            continue;
        }
        // The command id must be loaded with `mov esi, imm32` shortly before the call.
        bool id_matches = false;
        for (size_t back = 5; back <= 16 && back <= p && !id_matches; ++back) {
            const uint8_t* insn = text + p - back;
            uint32_t imm;
            std::memcpy(&imm, insn + 1, sizeof imm);
            id_matches = insn[0] == 0xBE && imm == kCommandIds[found];
        }
        if (!id_matches) {
            error = "GetInputValue call #" + std::to_string(found) + " has an unexpected command id";
            return false;
        }
        out.get_input_value_calls[found++] = text_address + p;
    }
    if (found != 4) {
        error = "found " + std::to_string(found) + " of 4 GetInputValue calls";
        return false;
    }
    return true;
}

bool FindExecuteCameraCommand(const uint8_t* text, size_t size, uintptr_t text_address,
    uintptr_t& out, std::string& error)
{
    // Entry: Command* (rdx) is spilled right away, then the entity handle at [rsi] is checked.
    const auto entry = FindPattern(text, size,
        "55 41 57 41 56 41 55 41 54 53 48 81 EC ?? ?? ?? ?? 48 89 54 24 ?? 48 8B 16 31 C0 48 89 D1 48 C1 E9 36 83 F9 40");
    if (entry.size() != 1) {
        error = "ExecuteCameraCommand signature matched " + std::to_string(entry.size()) + " times, expected 1";
        return false;
    }
    // The same function must contain the camera move check, `lea esi, [rdx-0x63] ; cmp esi, 3 ; jbe`:
    // command id - CameraBackward (99) <= CameraRight (102) - CameraBackward.
    const auto check = FindPattern(text, size, "8D 72 9D 83 FE 03 0F 86 ?? ?? ?? ?? 31 ED 8D 4A 9F");
    if (check.size() != 1 || check[0] < entry[0] || check[0] - entry[0] > 0x2000) {
        error = "ExecuteCameraCommand camera move check not found inside the function";
        return false;
    }
    out = text_address + entry[0];
    return true;
}

bool FindRotationAddresses(const uint8_t* text, size_t size, uintptr_t text_address,
    uintptr_t execute_camera_command, RotationAddresses& out, std::string& error)
{
    auto starts_with = [&](int64_t offset, const char* pattern) {
        const auto bytes = ParsePattern(pattern);
        if (offset < 0 || static_cast<size_t>(offset) + bytes.size() > size) {
            return false;
        }
        for (size_t i = 0; i < bytes.size(); ++i) {
            if (bytes[i] >= 0 && text[offset + i] != bytes[i]) {
                return false;
            }
        }
        return true;
    };

    // Every `mov esi, 0x2E ; call rel32` in the game; the push and pop functions are told apart by
    // their first instructions (push appends to the cursor array at +0x30, pop searches it).
    const auto sites = FindPattern(text, size, "BE 2E 00 00 00 E8", SIZE_MAX);
    const size_t camera_offset = execute_camera_command - text_address;
    for (const size_t site : sites) {
        const size_t call = site + 5;
        const int64_t target = RelTarget(text, call + 1, call + 5);
        const bool in_camera_command = site >= camera_offset && site < camera_offset + 0x2000;
        if (starts_with(target, "55 53 50 48 89 F8 48 8B 7F 30")) {
            if (in_camera_command) {
                out.cursor_push = text_address + target;
            }
            out.push_calls.push_back(text_address + call);
        } else if (starts_with(target, "48 8B 47 30 48 85 C0 74")) {
            if (in_camera_command) {
                out.cursor_pop = text_address + target;
            }
            out.pop_calls.push_back(text_address + call);
        }
    }
    // Keep only the calls to the functions ExecuteCameraCommand itself uses.
    std::erase_if(out.push_calls, [&](uintptr_t call) {
        return text_address + RelTarget(text, call - text_address + 1, call - text_address + 5) != out.cursor_push;
    });
    std::erase_if(out.pop_calls, [&](uintptr_t call) {
        return text_address + RelTarget(text, call - text_address + 1, call - text_address + 5) != out.cursor_pop;
    });
    if (!out.cursor_push || !out.cursor_pop || out.push_calls.empty() || out.pop_calls.empty()) {
        error = "rotation cursor push/pop calls not found in ExecuteCameraCommand";
        return false;
    }

    // The SDL event loop converts the mouse position to floats and asks the input device for the
    // virtual cursor position, which it then stores: `call [rax+slot] ; movlps [rbx], xmm0`.
    const auto virtual_cursor = FindPattern(text, size, "0F 5B C0 0F 13 44 24 ?? 48 8B 07 FF 90 ?? ?? ?? ?? 0F 13 03");
    if (virtual_cursor.size() != 1) {
        error = "SetVirtualCursorPos signature matched " + std::to_string(virtual_cursor.size()) + " times, expected 1";
        return false;
    }
    out.set_virtual_cursor_call = text_address + virtual_cursor[0] + 11;
    std::memcpy(&out.set_virtual_cursor_slot, text + virtual_cursor[0] + 13, sizeof out.set_virtual_cursor_slot);
    return true;
}

bool FindUnclickableCharacterAddresses(const uint8_t* text, size_t size, uintptr_t text_address,
    UnclickableCharacterAddresses& out, std::string& error)
{
    // IsFading: `test dl, dl ; jne`, then the handle's type index (`shr rax, 0x36`).
    const auto fading = FindPattern(text, size,
        "55 41 57 41 56 41 55 41 54 53 48 83 EC 58 49 89 FE 84 D2 0F 85 ?? ?? ?? ?? 4C 8B 16 4C 89 F0 48 89 74 24 "
        "50 48 C1 E8 36");
    // OnSelectedCharacterChanged keeps player, this, the bool and the EntityRef*, then reads +0x888.
    const auto selected = FindPattern(text, size,
        "55 41 57 41 56 41 55 41 54 53 48 83 EC 58 41 89 F4 48 89 3C 24 41 89 CE 49 89 D7 66 44 89 64 24 0E 48 8B "
        "B7 88 08 00 00");
    if (fading.size() != 1 || selected.size() != 1) {
        error = "IsFading matched " + std::to_string(fading.size()) + " times, OnSelectedCharacterChanged " +
            std::to_string(selected.size()) + ", expected 1 each";
        return false;
    }
    out.is_fading = text_address + fading[0];
    out.selected_character_changed = text_address + selected[0];

    // Every `call IsFading`, without the sound occlusion callbacks (`xor al, 1` after the call) and the
    // character highlights (`call [rax+0xA8] ; mov rdi, [rax]` just before it).
    static constexpr uint8_t kHighlight[] = { 0xFF, 0x90, 0xA8, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x38 };
    int others = 0;
    for (size_t i = 0; i + 7 <= size; ++i) {
        if (text[i] != 0xE8 || static_cast<size_t>(RelTarget(text, i + 1, i + 5)) != fading[0]) {
            continue;
        }
        const bool sound = text[i + 5] == 0x34 && text[i + 6] == 0x01;
        const uint8_t* before = text + (i >= 16 ? i - 16 : 0);
        const bool highlight = std::search(before, text + i, std::begin(kHighlight), std::end(kHighlight)) != text + i;
        if (sound || highlight) {
            ++others;
        } else {
            out.picking_calls.push_back(text_address + i);
        }
    }
    if (out.picking_calls.size() != 7 || others != 4) {
        error = "IsFading has " + std::to_string(out.picking_calls.size()) + " picking calls and " +
            std::to_string(others) + " others, expected 7 and 4";
        return false;
    }
    return true;
}

bool FindOccluderAddresses(const uint8_t* text, size_t size, uintptr_t text_address, OccluderAddresses& out,
    std::string& error)
{
    // GetCameraPitchDegrees: `cmp byte [rdi+0x1E4], 0 ; je ; test sil, sil ; je`, then the camera mode
    // (`cmp dword [rdi+0x13C], 2`) and the flags at +0xA8.
    const auto pitch = FindPattern(text, size,
        "80 BF E4 01 00 00 00 74 ?? 40 84 F6 0F 84 ?? ?? ?? ?? 48 8B 05 ?? ?? ?? ?? 83 BF 3C 01 00 00 02 8A 8F A8 "
        "00 00 00");
    // The picking ray's last stack argument (mask 0x210080), then the 11 bytes the hook replaces.
    const auto ray = FindPattern(text, size, "C7 04 24 80 00 21 00 48 8B 8C 24 B0 00 00 00 FF 50 48");
    if (pitch.size() != 1 || ray.size() != 1) {
        error = "GetCameraPitchDegrees matched " + std::to_string(pitch.size()) + " times, the picking ray " +
            std::to_string(ray.size()) + ", expected 1 each";
        return false;
    }
    // The same call also gets `mov r9d, 0x4CA20` (the other mask) shortly before.
    static constexpr uint8_t kMask[] = { 0x41, 0xB9, 0x20, 0xCA, 0x04, 0x00 };
    const uint8_t* before = text + (ray[0] >= 0x100 ? ray[0] - 0x100 : 0);
    if (std::search(before, text + ray[0], std::begin(kMask), std::end(kMask)) == text + ray[0]) {
        error = "the picking ray call has no mov r9d, 0x4CA20 before it";
        return false;
    }
    out.camera_pitch = text_address + pitch[0];
    out.raycast_site = text_address + ray[0] + 7;
    return true;
}

bool FindCameraTweakAddresses(const uint8_t* text, size_t size, uintptr_t text_address, CameraTweakAddresses& out,
    std::string& error)
{
    const auto site = FindPattern(text, size,
        "48 8B 1D ?? ?? ?? ?? 80 BB ?? ?? 00 00 00 0F 85 ?? ?? ?? ?? F6 85 A8 00 00 00 01 0F 85 ?? ?? ?? ?? 48 81 C3 "
        "?? ?? ?? ?? 48 89 EF 31 F6 E8 ?? ?? ?? ?? 80 BD 4C 01 00 00 00");
    if (site.size() != 1) {
        error = "the camera pitch update matched " + std::to_string(site.size()) + " times, expected 1";
        return false;
    }
    const size_t s = site[0];
    // GetCameraPitchDegrees starts with `cmp byte [rdi+0x1E4], 0`.
    static constexpr uint8_t kPitchEntry[] = { 0x80, 0xBF, 0xE4, 0x01, 0x00, 0x00, 0x00 };
    const int64_t pitch = RelTarget(text, s + 46, s + 50);
    if (pitch < 0 || static_cast<size_t>(pitch) + sizeof kPitchEntry > size ||
        std::memcmp(text + pitch, kPitchEntry, sizeof kPitchEntry) != 0) {
        error = "the camera pitch update doesn't call GetCameraPitchDegrees";
        return false;
    }
    // Both jne land on `add rbx, offset ; jmp back` to the `mov rdi, rbp`.
    auto branch_offset = [&](size_t jne_field, size_t jne_end, uint32_t& offset) {
        const int64_t target = RelTarget(text, jne_field, jne_end);
        if (target < 0 || static_cast<size_t>(target) + 12 > size || text[target] != 0x48 || text[target + 1] != 0x81 ||
            text[target + 2] != 0xC3 || text[target + 7] != 0xE9 ||
            RelTarget(text, target + 8, target + 12) != static_cast<int64_t>(s + 40)) {
            return false;
        }
        offset = ReadRel32(text + target + 3);
        return true;
    };
    if (!branch_offset(s + 16, s + 20, out.override_definition) || !branch_offset(s + 29, s + 33, out.combat)) {
        error = "the camera definition branches of the pitch update don't match";
        return false;
    }
    out.settings_pointer = text_address + RelTarget(text, s + 3, s + 7);
    out.override_flag = ReadRel32(text + s + 9);
    out.exploration = ReadRel32(text + s + 36);
    out.camera_pitch = text_address + pitch;
    out.pitch_return = text_address + s + 50;
    return true;
}

bool FindCharacterCameraAddresses(const uint8_t* text, size_t size, uintptr_t text_address,
    uintptr_t is_in_controller_mode, CharacterCameraAddresses& out, std::string& error)
{
    // Inlined GetViewport found a viewport (r12), then: `cmp byte [IsInControllerMode], 0 ; jne skip`
    // and the edge panning code reads the viewport at +0x21A8.
    const auto edge = FindPattern(text, size,
        "4D 85 E4 0F 84 ?? ?? ?? ?? 80 3D ?? ?? ?? ?? 00 0F 85 ?? ?? ?? ?? 41 8B 8C 24 A8 21 00 00");
    if (edge.size() != 1) {
        error = "edge panning signature matched " + std::to_string(edge.size()) + " times, expected 1";
        return false;
    }
    if (text_address + RelTarget(text, edge[0] + 11, edge[0] + 16) != is_in_controller_mode) {
        error = "edge panning check doesn't read IsInControllerMode";
        return false;
    }
    out.edge_panning_jne = text_address + edge[0] + 16;

    // The bounds test of the chase camera target; `xor al, 1` inverts its result right after.
    const auto bounds = FindPattern(text, size, "48 8B 79 20 48 8B 71 28 48 83 C2 3C E8 ?? ?? ?? ?? 34 01");
    if (bounds.size() != 1) {
        error = "chase camera signature matched " + std::to_string(bounds.size()) + " times, expected 1";
        return false;
    }
    out.bounds_call = text_address + bounds[0] + 12;
    return true;
}

bool FindTelemetryCalls(const uint8_t* text, size_t text_size, uintptr_t text_address, const uint8_t* rodata,
    size_t rodata_size, uintptr_t rodata_address, TelemetryCalls& out, std::string& error)
{
    // Every place each name sits in .rodata (the linker may share it with a longer string).
    std::vector<std::pair<uintptr_t, int>> names;
    for (int event = 0; event < TelemetryCalls::kEventCount; ++event) {
        const char* name = TelemetryCalls::kNames[event];
        const size_t length = std::strlen(name) + 1;  // with the terminating NUL
        for (size_t pos = 0; pos + length <= rodata_size; ++pos) {
            const void* found = memmem(rodata + pos, rodata_size - pos, name, length);
            if (!found) {
                break;
            }
            pos = static_cast<const uint8_t*>(found) - rodata;
            names.emplace_back(rodata_address + pos, event);
        }
    }

    // One pass over the code: `lea rdx, [rip+name]`, then the first `call rel32` within 16 bytes.
    int hits[TelemetryCalls::kEventCount] = {};
    for (size_t p = 0; p + 7 <= text_size; ++p) {
        const void* lea = memmem(text + p, text_size - p, "\x48\x8D\x15", 3);
        if (!lea) {
            break;
        }
        p = static_cast<const uint8_t*>(lea) - text;
        if (p + 7 > text_size) {
            break;
        }
        const uintptr_t target = text_address + RelTarget(text, p + 3, p + 7);
        int event = -1;
        for (const auto& [address, name_event] : names) {
            if (address == target) {
                event = name_event;
            }
        }
        if (event < 0) {
            continue;
        }
        for (size_t c = p + 7; c < p + 7 + 16 && c + 5 <= text_size; ++c) {
            if (text[c] != 0xE8) {
                continue;
            }
            const uintptr_t callee = text_address + RelTarget(text, c + 1, c + 5);
            if (out.log_telemetry && callee != out.log_telemetry) {
                error = std::string("\"") + TelemetryCalls::kNames[event] +
                        "\" is passed to a different function than the other events";
                return false;
            }
            out.log_telemetry = callee;
            out.calls[event] = text_address + c;
            out.strings[event] = target;
            ++hits[event];
            break;
        }
    }
    for (int event = 0; event < TelemetryCalls::kEventCount; ++event) {
        if (hits[event] != 1) {
            error = std::string("telemetry event \"") + TelemetryCalls::kNames[event] + "\": " +
                    std::to_string(hits[event]) + " calls, expected 1";
            return false;
        }
    }
    return true;
}

bool FindCameraSpeedStores(const uint8_t* text, size_t size, uintptr_t text_address,
    uintptr_t execute_camera_command, CameraSpeedStores& out, std::string& error)
{
    // Searched only inside ExecuteCameraCommand (about 0x1100 bytes). Both directions of a command
    // end at the same store: the speed, then `mulss` by the input, then the store.
    constexpr size_t kWindow = 0x1800;
    const size_t start = execute_camera_command - text_address;
    if (execute_camera_command < text_address || start >= size) {
        error = "ExecuteCameraCommand is outside the code";
        return false;
    }
    const size_t window = std::min(kWindow, size - start);
    // mulss xmm0, xmm1 ; movss [r8+0xA4], xmm0 ; jmp
    const auto zoom = FindPattern(text + start, window, "F3 0F 59 C1 F3 41 0F 11 80 A4 00 00 00 E9");
    // mulss xmm1, xmm0 ; movss [r8+0x9C], xmm1 ; mov byte [r8+0x1F1], 1 ; jmp
    const auto rotation =
        FindPattern(text + start, window, "F3 0F 59 C8 F3 41 0F 11 88 9C 00 00 00 41 C6 80 F1 01 00 00 01 E9");
    if (zoom.size() != 1 || rotation.size() != 1) {
        error = "the zoom speed store matched " + std::to_string(zoom.size()) + " times, the rotation speed store " +
            std::to_string(rotation.size()) + ", expected 1 each";
        return false;
    }
    out.zoom = execute_camera_command + zoom[0] + 4;
    out.rotation = execute_camera_command + rotation[0] + 4;
    return true;
}

}  // namespace wasd
