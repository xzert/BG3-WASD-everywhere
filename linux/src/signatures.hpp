#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace wasd {

// Game addresses the mod needs, found by signature.
struct GameAddresses {
    // `cmp byte ptr [IsInControllerMode], 0 ; je` in UpdateWasdMove2 (kWasdUnlockSize bytes).
    uintptr_t wasd_unlock = 0;
    // The bool global that cmp reads.
    uintptr_t is_in_controller_mode = 0;
    // The four `call GetInputValue` in GetMovementInputWorldVec, for command ids 0xA0, 0x9F, 0x9D, 0x9E.
    std::array<uintptr_t, 4> get_input_value_calls{};
    uintptr_t get_input_value = 0;
};

constexpr size_t kWasdUnlockSize = 13;

// Searches the game's code. `text_address` is the address that `text[0]` corresponds to, so the
// same search runs on live memory (in the game) and on the file (in selftest).
bool FindGameAddresses(const uint8_t* text, size_t size, uintptr_t text_address,
    GameAddresses& out, std::string& error);

// ExecuteCameraCommand(this in rdi, context in rsi, Command* in rdx). The game calls it through a
// vtable, so the mod hooks it by replacing its vtable slot. Optional: movement works without it.
bool FindExecuteCameraCommand(const uint8_t* text, size_t size, uintptr_t text_address,
    uintptr_t& out, std::string& error);

// Camera rotation. Rotation pushes the rotation cursor (id 0x2E) onto the cursor manager when it
// starts and pops it when it stops; the Windows mod calls these CameraRotationOn/Off.
struct RotationAddresses {
    uintptr_t cursor_push = 0;
    uintptr_t cursor_pop = 0;
    std::vector<uintptr_t> push_calls;  // `mov esi, 0x2E ; call cursor_push`
    std::vector<uintptr_t> pop_calls;   // `mov esi, 0x2E ; call cursor_pop`
    // `call qword ptr [rax+slot]` (6 bytes) to SetVirtualCursorPos in the SDL event loop.
    uintptr_t set_virtual_cursor_call = 0;
    uint32_t set_virtual_cursor_slot = 0;
};

bool FindRotationAddresses(const uint8_t* text, size_t size, uintptr_t text_address,
    uintptr_t execute_camera_command, RotationAddresses& out, std::string& error);

// The game logs telemetry events as LogTelemetry(rdi, rsi, const char* name in rdx). The calls for
// these events tell the mod when combat, forced turn-based mode (FTB) and dialogs start and end.
struct TelemetryCalls {
    enum Event { CombatStart, CombatEnd, FtbStart, FtbEnd, DialogStart, DialogEnd, kEventCount };
    static constexpr const char* kNames[kEventCount] = { "combat_start", "combat_end", "ftb_start", "ftb_end",
        "dialog_start", "dialog_end" };

    uintptr_t log_telemetry = 0;
    uintptr_t calls[kEventCount] = {};    // `call LogTelemetry` right after `lea rdx, [name]`
    uintptr_t strings[kEventCount] = {};  // address of each name, as passed in rdx
};

bool FindTelemetryCalls(const uint8_t* text, size_t text_size, uintptr_t text_address, const uint8_t* rodata,
    size_t rodata_size, uintptr_t rodata_address, TelemetryCalls& out, std::string& error);

// The camera in Character mode. On Linux, UpdateCameraPosition (with GetViewport) is inlined into
// UpdateCamera, where the Windows mod hooks BlockEdgePanningHook and PreventLosingChaseCamTargetHook.
struct CharacterCameraAddresses {
    // `jne skip` (0F 85 rel32) after `cmp byte [IsInControllerMode], 0`: edge panning is skipped with
    // a gamepad. Rewritten to `nop ; jmp skip` (90 E9), edge panning is always skipped.
    uintptr_t edge_panning_jne = 0;
    // `call IsPointInsideEntityBounds(bounds in rdi:rsi, point in rdx)` for the chase camera target.
    uintptr_t bounds_call = 0;
};

bool FindCharacterCameraAddresses(const uint8_t* text, size_t size, uintptr_t text_address,
    uintptr_t is_in_controller_mode, CharacterCameraAddresses& out, std::string& error);

// The unclickable active character (PreventClickingActiveCharacter). The game's picking skips
// objects that ls::IsFading reports as fading out; the macOS build keeps these names.
struct UnclickableCharacterAddresses {
    // ls::IsFading(entity handle in rdi, world view in rsi, flag in dl) -> al. Its name says the
    // handle is passed by reference, but the optimizer passes the value.
    uintptr_t is_fading = 0;
    // Its seven `call rel32` from ecl::PickingHelper. The other four (character highlights and sound
    // occlusion) aren't included.
    std::vector<uintptr_t> picking_calls;
    // gui::GameUI::OnSelectedCharacterChanged(this, short player, const ecs::EntityRef*, bool); the
    // EntityRef starts with the handle. The game calls it through a signal.
    uintptr_t selected_character_changed = 0;
};

bool FindUnclickableCharacterAddresses(const uint8_t* text, size_t size, uintptr_t text_address,
    UnclickableCharacterAddresses& out, std::string& error);

// Camera occluders (PreventClickingCameraOccluders). The macOS build keeps these names.
struct OccluderAddresses {
    // ecl::GameCameraBehavior::GetCameraPitchDegrees(this, bool, bool) -> xmm0. The camera update
    // calls it every frame, so its `this` is the camera; it starts with `cmp byte [rdi+0x1E4], 0`.
    uintptr_t camera_pitch = 0;
    // In ecl::PickingHelper::GameObjectPick::ExecuteWTKernel, the picking ray:
    // `mov rcx, [rsp+0xB0] ; call [rax+0x48]` (11 bytes), the call of phx::PhysXScene::RaycastAll(this,
    // start in rsi, end in rdx, ...). Windows calls the same function through slot 0x40: MSVC has one
    // destructor slot where the Itanium ABI has two.
    uintptr_t raycast_site = 0;
};

bool FindOccluderAddresses(const uint8_t* text, size_t size, uintptr_t text_address, OccluderAddresses& out,
    std::string& error);

// Camera tweaks (Native Camera Tweaks). The camera update has UpdateCameraPitch inlined; it asks
// GetCameraPitchDegrees for the pitch the camera turns to, with the current camera definition in rbx:
// `mov rbx, [settings] ; cmp byte [rbx+flag], 0 ; jne override ; test byte [rbp+0xA8], 1 ;
// jne combat ; add rbx, exploration ; mov rdi, rbp ; xor esi, esi ; call GetCameraPitchDegrees`.
// Windows: GetCurrentCameraDefinition, with the offsets 0x28 lower.
struct CameraTweakAddresses {
    uintptr_t settings_pointer = 0;  // the global pointer to eoc::CameraSettings
    uint32_t override_flag = 0;
    uint32_t exploration = 0;
    uint32_t combat = 0;
    uint32_t override_definition = 0;
    uintptr_t camera_pitch = 0;  // GetCameraPitchDegrees, as in OccluderAddresses
    uintptr_t pitch_return = 0;  // the return address of that call
};

bool FindCameraTweakAddresses(const uint8_t* text, size_t size, uintptr_t text_address, CameraTweakAddresses& out,
    std::string& error);

// Inside ExecuteCameraCommand, where the zoom and keyboard rotation commands write their speed into
// the camera: `movss [r8+0xA4], xmm0` and `movss [r8+0x9C], xmm1`, 9 bytes each.
struct CameraSpeedStores {
    uintptr_t zoom = 0;
    uintptr_t rotation = 0;
};

bool FindCameraSpeedStores(const uint8_t* text, size_t size, uintptr_t text_address,
    uintptr_t execute_camera_command, CameraSpeedStores& out, std::string& error);

}  // namespace wasd
