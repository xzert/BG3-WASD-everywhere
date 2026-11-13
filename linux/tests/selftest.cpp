// Runs the mod's signature searches on a bg3 binary on disk, so a new game build can be checked
// without starting the game. With --inputconfig, runs the key binding patcher on a file instead.
//
// Usage: selftest <path to bg3>
//        selftest --inputconfig <inputconfig_p1.json> <output file>
#include <elf.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "inputconfig.hpp"
#include "signatures.hpp"

namespace {

struct Segment {
    const uint8_t* data = nullptr;
    size_t size = 0;
    uintptr_t address = 0;
};

unsigned long Hex(uintptr_t value) { return static_cast<unsigned long>(value); }

}  // namespace

int PatchInputconfig(const char* in_path, const char* out_path)
{
    std::ifstream in_file(in_path, std::ios::binary);
    const std::string in((std::istreambuf_iterator<char>(in_file)), std::istreambuf_iterator<char>());
    std::string out;
    const wasd::inputconfig::Result result = wasd::inputconfig::PatchText(in, out);
    if (!result.ok) {
        std::printf("FAILED: %s\n", result.error.c_str());
        return 1;
    }
    std::ofstream(out_path, std::ios::binary) << out;
    auto print = [](const char* label, const std::vector<std::string>& keys) {
        std::printf("%s:", label);
        for (const std::string& key : keys) {
            std::printf(" %s", key.c_str());
        }
        std::printf("\n");
    };
    std::printf("changed: %s\n", result.changed ? "yes" : "no");
    print("forward keys", result.forward_keys);
    print("backward keys", result.backward_keys);
    std::printf("rotation on left click: %s\n", result.rotate_includes_left_click ? "yes" : "no");
    return 0;
}

int main(int argc, char** argv)
{
    if (argc == 4 && std::strcmp(argv[1], "--inputconfig") == 0) {
        return PatchInputconfig(argv[2], argv[3]);
    }
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <path to bg3>\n", argv[0]);
        return 2;
    }

    std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
    if (!file) {
        std::fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }
    std::vector<uint8_t> data(static_cast<size_t>(file.tellg()));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));

    Elf64_Ehdr header;
    std::memcpy(&header, data.data(), sizeof header);
    if (std::memcmp(header.e_ident, ELFMAG, SELFMAG) != 0 || header.e_ident[EI_CLASS] != ELFCLASS64) {
        std::fprintf(stderr, "%s is not a 64-bit ELF file\n", argv[1]);
        return 2;
    }

    // Same choice as the mod at runtime: the executable segment and the largest read-only one.
    Segment text, rodata;
    for (int i = 0; i < header.e_phnum; ++i) {
        Elf64_Phdr ph;
        std::memcpy(&ph, data.data() + header.e_phoff + i * header.e_phentsize, sizeof ph);
        if (ph.p_type != PT_LOAD) {
            continue;
        }
        const Segment segment{ data.data() + ph.p_offset, ph.p_filesz, ph.p_vaddr };
        if (ph.p_flags & PF_X) {
            text = segment;
        } else if (!(ph.p_flags & PF_W) && segment.size > rodata.size) {
            rodata = segment;
        }
    }
    if (!text.data || !rodata.data) {
        std::fprintf(stderr, "code or read-only data segment not found\n");
        return 1;
    }

    const auto start = std::chrono::steady_clock::now();
    std::string error;
    wasd::GameAddresses game;
    if (!wasd::FindGameAddresses(text.data, text.size, text.address, game, error)) {
        std::printf("FAILED: %s\n", error.c_str());
        return 1;
    }
    std::printf("WASD unlock          %#lx\n", Hex(game.wasd_unlock));
    std::printf("IsInControllerMode   %#lx\n", Hex(game.is_in_controller_mode));
    std::printf("GetInputValue        %#lx, called from", Hex(game.get_input_value));
    for (const uintptr_t call : game.get_input_value_calls) {
        std::printf(" %#lx", Hex(call));
    }
    std::printf("\n");

    // The optional features are checked too: a failure here means the feature stays off in game.
    int failures = 0;
    uintptr_t camera = 0;
    if (!wasd::FindExecuteCameraCommand(text.data, text.size, text.address, camera, error)) {
        std::printf("FAILED (camera block and rotation): %s\n", error.c_str());
        ++failures;
    } else {
        std::printf("ExecuteCameraCommand %#lx\n", Hex(camera));
        wasd::CameraSpeedStores stores;
        if (!wasd::FindCameraSpeedStores(text.data, text.size, text.address, camera, stores, error)) {
            std::printf("FAILED (camera tweak speeds): %s\n", error.c_str());
            ++failures;
        } else {
            std::printf("Camera speed stores  zoom %#lx, rotation %#lx\n", Hex(stores.zoom), Hex(stores.rotation));
        }
        wasd::RotationAddresses rotation;
        if (!wasd::FindRotationAddresses(text.data, text.size, text.address, camera, rotation, error)) {
            std::printf("FAILED (camera rotation): %s\n", error.c_str());
            ++failures;
        } else {
            std::printf("Rotation cursor push %#lx, called from", Hex(rotation.cursor_push));
            for (const uintptr_t call : rotation.push_calls) {
                std::printf(" %#lx", Hex(call));
            }
            std::printf("\nRotation cursor pop  %#lx, called from", Hex(rotation.cursor_pop));
            for (const uintptr_t call : rotation.pop_calls) {
                std::printf(" %#lx", Hex(call));
            }
            std::printf("\nSetVirtualCursorPos  call at %#lx, vtable slot %#x\n", Hex(rotation.set_virtual_cursor_call),
                rotation.set_virtual_cursor_slot);
        }
    }

    wasd::CharacterCameraAddresses character_camera;
    if (!wasd::FindCharacterCameraAddresses(text.data, text.size, text.address, game.is_in_controller_mode,
            character_camera, error)) {
        std::printf("FAILED (edge panning and chase camera): %s\n", error.c_str());
        ++failures;
    } else {
        std::printf("Edge panning jne     %#lx, chase camera bounds call %#lx\n",
            Hex(character_camera.edge_panning_jne), Hex(character_camera.bounds_call));
    }

    wasd::UnclickableCharacterAddresses unclickable;
    if (!wasd::FindUnclickableCharacterAddresses(text.data, text.size, text.address, unclickable, error)) {
        std::printf("FAILED (unclickable character): %s\n", error.c_str());
        ++failures;
    } else {
        std::printf("IsFading             %#lx, picking calls", Hex(unclickable.is_fading));
        for (const uintptr_t call : unclickable.picking_calls) {
            std::printf(" %#lx", Hex(call));
        }
        std::printf("\nOnSelectedCharacterChanged %#lx\n", Hex(unclickable.selected_character_changed));
    }

    wasd::OccluderAddresses occluders;
    if (!wasd::FindOccluderAddresses(text.data, text.size, text.address, occluders, error)) {
        std::printf("FAILED (camera occluders): %s\n", error.c_str());
        ++failures;
    } else {
        std::printf("GetCameraPitchDegrees %#lx, picking ray %#lx, walkable ray %#lx\n", Hex(occluders.camera_pitch),
            Hex(occluders.raycast_site), Hex(occluders.walkable_ray_site));
        if (!occluders.walkable_ray_site) {
            std::printf("FAILED (camera occluders): the walkable ray matched 0 or several times\n");
            ++failures;
        }
    }

    wasd::InteractMoveAddresses interact;
    if (!wasd::FindInteractMoveAddresses(text.data, text.size, text.address, interact, error)) {
        std::printf("FAILED (BlockInteractMove): %s\n", error.c_str());
        ++failures;
    } else {
        std::printf("Interact move         click check %#lx, hold flag %#lx\n", Hex(interact.click_move_je),
            Hex(interact.hold_move_flag));
    }

    wasd::CameraTweakAddresses tweaks;
    if (!wasd::FindCameraTweakAddresses(text.data, text.size, text.address, tweaks, error)) {
        std::printf("FAILED (camera tweaks): %s\n", error.c_str());
        ++failures;
    } else if (tweaks.camera_pitch != occluders.camera_pitch) {
        std::printf("FAILED (camera tweaks): the pitch update calls %#lx, not GetCameraPitchDegrees\n",
            Hex(tweaks.camera_pitch));
        ++failures;
    } else {
        std::printf("Camera settings      %#lx, flag +%#x, definitions +%#x +%#x +%#x, pitch update returns to %#lx\n",
            Hex(tweaks.settings_pointer), tweaks.override_flag, tweaks.exploration, tweaks.combat,
            tweaks.override_definition, Hex(tweaks.pitch_return));
    }

    wasd::TelemetryCalls telemetry;
    if (!wasd::FindTelemetryCalls(text.data, text.size, text.address, rodata.data, rodata.size, rodata.address,
            telemetry, error)) {
        std::printf("FAILED (combat and turn-based events): %s\n", error.c_str());
        ++failures;
    } else {
        std::printf("LogTelemetry         %#lx, events:", Hex(telemetry.log_telemetry));
        for (int event = 0; event < wasd::TelemetryCalls::kEventCount; ++event) {
            std::printf(" %s@%#lx", wasd::TelemetryCalls::kNames[event], Hex(telemetry.calls[event]));
        }
        std::printf("\n");
    }

    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    std::printf("searched %.0f MiB of code in %ld ms\n", text.size / 1048576.0, static_cast<long>(ms.count()));
    return failures ? 1 : 0;
}
