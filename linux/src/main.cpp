// BG3 WASD Everywhere for the native Linux build of Baldur's Gate 3, loaded with LD_PRELOAD.
// What is ported and what isn't: see linux/README.md.
#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

#include "camera.hpp"
#include "camera_tweaks.hpp"
#include "input.hpp"
#include "inputconfig.hpp"
#include "log.hpp"
#include "memory.hpp"
#include "picking.hpp"
#include "settings.hpp"
#include "signatures.hpp"
#include "state.hpp"

// The picking ray hook and the camera speed stores, written in assembly at the end of this file.
extern "C" __attribute__((visibility("hidden"))) void wasd_RaycastSiteStub();
extern "C" __attribute__((visibility("hidden"))) void wasd_WalkableRaySiteStub();
extern "C" __attribute__((visibility("hidden"))) void wasd_ZoomSpeedStub();
extern "C" __attribute__((visibility("hidden"))) void wasd_RotationSpeedStub();

// The multipliers the speed stores apply (camera tweaks). BlockCameraMoveCommands sets them before
// each camera command, on the thread that runs the command and the stores.
extern "C" {
__attribute__((visibility("hidden"))) float wasd_zoom_speed_mult = 1.0f;
__attribute__((visibility("hidden"))) float wasd_rotation_speed_mult = 1.0f;
}

namespace wasd {
namespace {

constexpr uint32_t kCharacterMoveForward = 157;
constexpr int kFramesToCenterCamera = 10;

// GetInputValue(controller in rdi, command id in esi, player id in edx) returns the command's value
// in xmm0. The mod changes the low two floats; the whole register is passed through so the rest of
// it stays as the game left it. All six integer argument registers are forwarded, so the original
// still gets everything if the game passes more than the three arguments seen at the call sites.
using Vec4 = float __attribute__((vector_size(16)));
using GetInputValueFn = Vec4 (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

GetInputValueFn g_get_input_value = nullptr;

// Port of OverrideMovementInputHook. Runs for each of the four CharacterMove* reads per frame.
Vec4 OverrideMovementInput(uint64_t controller, uint64_t command, uint64_t player_id, uint64_t a3,
    uint64_t a4, uint64_t a5)
{
    Vec4 value = g_get_input_value(controller, command, player_id, a3, a4, a5);
    const auto command_id = static_cast<uint32_t>(command);
    State& state = GetState();
    state.last_movement_poll_ms = NowMs();
    if (state.IsInControllerMode()) {
        return value;  // leave gamepad input alone
    }

    auto set = [&](float x, float y) {
        value[0] = x;
        value[1] = y;
    };
    if (state.autoforward && command_id == kCharacterMoveForward) {
        set(1.0f, 1.0f);
    }
    if (state.rotate_plus_lmb_is_forward && !state.rotate_keys_include_lmb) {
        // Holding the rotate button and the left mouse button walks forward, like in MMOs.
        const bool rotate_and_lmb = state.mouse_left_down && camera::IsRotating();
        if (state.rotate_and_lmb_last_frame && !rotate_and_lmb) {
            state.autoforward = false;
        }
        if (rotate_and_lmb && command_id == kCharacterMoveForward) {
            set(1.0f, 1.0f);
        }
        state.rotate_and_lmb_last_frame = rotate_and_lmb;
    }
    if (state.walking_toggled != state.walking_held) {
        const float speed = state.walk_speed;
        set(value[0] * speed, value[1] * speed);
    }
    if (!state.IsCharacterMovementMode()) {
        set(0.0f, 0.0f);
    }
    if (command_id == kCharacterMoveForward) {
        int frames = state.frames_to_center_camera;
        if (frames > 0 && state.frames_to_center_camera.compare_exchange_strong(frames, frames - 1)) {
            set(1.0f, 1.0f);
        }
    }
    return value;
}

// ExecuteCameraCommand returns a small struct in rax (rdx is passed back as well, in case it is
// 16 bytes). Like GetInputValue, all six integer argument registers are forwarded.
struct RegisterPair {
    uint64_t rax, rdx;
};
using ExecuteCameraCommandFn = RegisterPair (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

void InstallUnclickableCharacter(const mem::MainExecutable& exe);
void InstallCameraFeatures(const mem::MainExecutable& exe, uintptr_t execute_camera_command);

constexpr uint32_t kCameraBackward = 99;
constexpr uint32_t kCameraRight = 102;

ExecuteCameraCommandFn g_execute_camera_command = nullptr;

// Port of the camera move part of ExecuteCameraCommandHook: in Character movement mode the
// movement keys drive the character, so their camera pan commands are dropped (id set to 0).
RegisterPair BlockCameraMoveCommands(uint64_t self, uint64_t context, uint64_t command, uint64_t a3,
    uint64_t a4, uint64_t a5)
{
    auto* command_id = reinterpret_cast<uint32_t*>(command);
    const State& state = GetState();
    if (command_id && *command_id >= kCameraBackward && *command_id <= kCameraRight &&
        state.IsCharacterMovementMode() && !state.in_combat && !state.IsInControllerMode()) {
        *command_id = 0;
    }
    // Zoom and rotation speed (camera tweaks), applied by the speed stores inside the command.
    const camera_tweaks::CommandSpeed speed =
        camera_tweaks::BeforeCameraCommand(reinterpret_cast<uint8_t*>(command), state.IsInControllerMode());
    wasd_zoom_speed_mult = speed.zoom;
    wasd_rotation_speed_mult = speed.keyboard_rotation;
    return g_execute_camera_command(self, context, command, a3, a4, a5);
}

using LogTelemetryFn = RegisterPair (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

LogTelemetryFn g_log_telemetry = nullptr;
TelemetryCalls g_telemetry;

// Port of the combat part of UpdateCameraHook and of FTBStartHook/FTBEndHook. The Windows mod reads
// combat from the camera's flags; here it comes from the game's own combat telemetry events, so a
// save loaded mid-combat isn't detected until the next combat change.
void OnGameEvent(uint64_t name)
{
    State& state = GetState();
    const bool auto_toggle = state.auto_toggle_movement_mode;
    const int64_t now = NowMs();
    // The game sends a combat event once per combatant, so a fight with 16 combatants starts 16
    // times within a moment. Repeats that soon are dropped. Later ones still count: the state can
    // be stale, e.g. after loading a save made outside combat while in combat.
    const bool combat_start = name == g_telemetry.strings[TelemetryCalls::CombatStart];
    const bool combat_end = name == g_telemetry.strings[TelemetryCalls::CombatEnd];
    const bool repeat = (combat_start && state.in_combat) || (combat_end && !state.in_combat);
    if (repeat && now - state.last_combat_change_ms < 2000) {
        return;
    }
    if (combat_start) {
        state.in_combat = true;
        state.last_combat_change_ms = now;
        LOG_INFO("Combat started");
        if (auto_toggle) {
            state.SetMovementModeToggled(false);
        }
    } else if (combat_end) {
        state.in_combat = false;
        state.last_combat_change_ms = now;
        LOG_INFO("Combat ended");
        if (auto_toggle) {
            state.SetMovementModeToggled(true);
        }
        if (state.walk_after_combat) {
            state.walking_toggled = true;
        }
    }
    if (combat_start || combat_end) {
        camera::UpdateCharacterModeCamera();  // the mode may not have changed, but combat did
    } else if (name == g_telemetry.strings[TelemetryCalls::FtbStart]) {
        LOG_INFO("Turn-based mode started");
        if (auto_toggle) {
            state.SetMovementModeToggled(false);
        }
    } else if (name == g_telemetry.strings[TelemetryCalls::FtbEnd]) {
        LOG_INFO("Turn-based mode ended");
        // Turn-based mode also ends when combat starts; don't switch back to Character mode then.
        if (auto_toggle && now - state.last_combat_change_ms > 500) {
            state.SetMovementModeToggled(true);
        }
    } else if (name == g_telemetry.strings[TelemetryCalls::DialogStart]) {
        LOG_INFO("Dialog started");
        camera::SetDialogActive(true);
    } else if (name == g_telemetry.strings[TelemetryCalls::DialogEnd]) {
        LOG_INFO("Dialog ended");
        camera::SetDialogActive(false);
    }
}

RegisterPair LogTelemetryHook(uint64_t a0, uint64_t a1, uint64_t name, uint64_t a3, uint64_t a4, uint64_t a5)
{
    const RegisterPair result = g_log_telemetry(a0, a1, name, a3, a4, a5);
    OnGameEvent(name);
    return result;
}

// Optional feature: on failure it logs a warning and the rest keeps working.
void InstallGameEvents(const mem::MainExecutable& exe)
{
    std::string error;
    if (!FindTelemetryCalls(reinterpret_cast<const uint8_t*>(exe.code.start), exe.code.size, exe.code.start,
            reinterpret_cast<const uint8_t*>(exe.rodata.start), exe.rodata.size, exe.rodata.start, g_telemetry,
            error)) {
        LOG_WARN("Movement mode won't follow combat and turn-based mode: %s", error.c_str());
        return;
    }
    void* stub = mem::MakeJumpStub(exe.code.start, reinterpret_cast<const void*>(&LogTelemetryHook));
    if (!stub) {
        LOG_WARN("Movement mode won't follow combat and turn-based mode: could not allocate a jump stub");
        return;
    }
    g_log_telemetry = reinterpret_cast<LogTelemetryFn>(g_telemetry.log_telemetry);
    for (const uintptr_t call : g_telemetry.calls) {
        mem::RedirectCall(call, stub);
    }
    LOG_INFO("Combat and turn-based mode events hooked (LogTelemetry %#lx)",
        static_cast<unsigned long>(g_telemetry.log_telemetry - exe.load_bias));
}

void OnMovementModeChanged(State& state)
{
    const bool character = state.IsCharacterMovementMode();
    if (character) {
        state.frames_to_center_camera = kFramesToCenterCamera;
    }
    camera::UpdateCharacterModeCamera();
    if (state.mode_toggles_mouselook) {
        camera::SetMouselook(character);
    }
    LOG_INFO("Movement mode: %s", character ? "character" : "camera");
}

bool IsGameProcess()
{
    char exe[PATH_MAX];
    const ssize_t length = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (length <= 0) {
        return false;
    }
    exe[length] = '\0';
    const char* name = std::strrchr(exe, '/');
    return name && std::strcmp(name + 1, "bg3") == 0;
}

// The base name of the config, the log and the log directory.
constexpr char kFileStem[] = "bg3-wasd-everywhere";

// ${XDG_STATE_HOME:-~/.local/state}/bg3-wasd-everywhere/bg3-wasd-everywhere.log. Not next to the
// library: inside the Steam Linux Runtime container, the directory of an LD_PRELOAD library is
// mounted read-only.
std::string LogPath()
{
    std::string dir;
    if (const char* state = std::getenv("XDG_STATE_HOME"); state && *state) {
        dir = state;
    } else if (const char* home = std::getenv("HOME"); home && *home) {
        dir = std::string(home) + "/.local/state";
    } else {
        return std::string("/tmp/") + kFileStem + ".log";
    }
    dir = dir + "/" + kFileStem;
    for (size_t slash = dir.find('/', 1); slash != std::string::npos; slash = dir.find('/', slash + 1)) {
        mkdir(dir.substr(0, slash).c_str(), 0755);
    }
    mkdir(dir.c_str(), 0755);
    return dir + "/" + kFileStem + ".log";
}

std::string LibraryDirectory()
{
    Dl_info info{};
    if (!dladdr(reinterpret_cast<void*>(&LibraryDirectory), &info) || !info.dli_fname) {
        return ".";
    }
    const std::string path = info.dli_fname;
    const auto slash = path.rfind('/');
    return slash == std::string::npos ? "." : path.substr(0, slash);
}

// Optional feature: on failure it logs a warning and movement keeps working. Returns the address of
// ExecuteCameraCommand (0 if not found), which the camera rotation features search from.
uintptr_t InstallCameraBlock(const mem::MainExecutable& exe)
{
    auto file_address = [&](uintptr_t address) { return static_cast<unsigned long>(address - exe.load_bias); };
    uintptr_t function = 0;
    std::string error;
    if (!FindExecuteCameraCommand(reinterpret_cast<const uint8_t*>(exe.code.start), exe.code.size,
            exe.code.start, function, error)) {
        LOG_WARN("Camera panning stays on in Character mode: %s", error.c_str());
        return 0;
    }
    const auto slots = mem::FindPointerSlots(exe, function);
    if (slots.size() != 1) {
        LOG_WARN("Camera panning stays on in Character mode: ExecuteCameraCommand has %zu vtable slots, expected 1",
            slots.size());
        return function;
    }
    g_execute_camera_command = reinterpret_cast<ExecuteCameraCommandFn>(function);
    if (!mem::WritePointer(exe, slots[0], reinterpret_cast<uintptr_t>(&BlockCameraMoveCommands))) {
        LOG_WARN("Camera panning stays on in Character mode: could not write the vtable slot");
        return function;
    }
    LOG_INFO("ExecuteCameraCommand %#lx hooked through vtable slot %#lx", file_address(function),
        file_address(slots[0]));
    return function;
}

bool Install()
{
    mem::MainExecutable exe;
    if (!mem::FindMainExecutable(exe)) {
        LOG_ERROR("Could not find the game's code in memory");
        return false;
    }

    GameAddresses game;
    std::string error;
    if (!FindGameAddresses(reinterpret_cast<const uint8_t*>(exe.code.start), exe.code.size,
            exe.code.start, game, error)) {
        LOG_ERROR("Signature search failed: %s", error.c_str());
        return false;
    }

    // Addresses as they appear in the bg3 file, to compare with a disassembler.
    auto file_address = [&](uintptr_t address) { return static_cast<unsigned long>(address - exe.load_bias); };
    LOG_INFO("WASD unlock %#lx, IsInControllerMode %#lx, GetInputValue %#lx",
        file_address(game.wasd_unlock), file_address(game.is_in_controller_mode),
        file_address(game.get_input_value));

    bool flag_in_data = false;
    for (int i = 0; i < exe.data_count; ++i) {
        flag_in_data |= exe.data[i].Contains(game.is_in_controller_mode);
    }
    if (!flag_in_data) {
        LOG_ERROR("IsInControllerMode is not inside the game's data segments");
        return false;
    }

    void* stub = mem::MakeJumpStub(exe.code.start, reinterpret_cast<const void*>(&OverrideMovementInput));
    if (!stub) {
        LOG_ERROR("Could not allocate a jump stub near the game code");
        return false;
    }

    g_get_input_value = reinterpret_cast<GetInputValueFn>(game.get_input_value);
    for (const uintptr_t site : game.get_input_value_calls) {
        if (mem::RedirectCall(site, stub) != game.get_input_value) {
            LOG_ERROR("Could not hook the GetInputValue call at %#lx", file_address(site));
            return false;
        }
    }

    uint8_t nops[kWasdUnlockSize];
    std::memset(nops, 0x90, sizeof nops);
    if (!mem::WriteCode(game.wasd_unlock, nops, sizeof nops)) {
        LOG_ERROR("Could not patch the WASD unlock");
        return false;
    }

    GetState().is_in_controller_mode = reinterpret_cast<const volatile bool*>(game.is_in_controller_mode);
    if (const uintptr_t execute_camera_command = InstallCameraBlock(exe)) {
        camera::Install(exe, execute_camera_command);
    }
    camera::InstallCharacterModeCamera(exe, game.is_in_controller_mode);
    camera::InstallInteractMoveBlocker(exe);
    InstallGameEvents(exe);
    InstallUnclickableCharacter(exe);
    // The camera speed stores only work through the ExecuteCameraCommand hook.
    InstallCameraFeatures(exe, g_execute_camera_command ? reinterpret_cast<uintptr_t>(g_execute_camera_command) : 0);
    return true;
}

// Unclickable active character, like PreventClickingActiveCharacterHook on Windows: in Character mode
// the picking code hears that the active character fades out, so the cursor ignores it. Only the
// picking code's calls of IsFading are redirected; character highlights and sound occlusion keep the
// game's answer. Picking runs on worker threads: only atomics and thread-locals here.
//
// A ray through the character comes down to the ground behind it; from a low camera, far behind, so a
// click there sent the character running off. When that point is further from the character than
// MaxClickDistanceBehindCharacter, the character stays clickable, as in vanilla. The ray comes from
// the camera occluders' stub, which sees GameObjectPick's RaycastAll just before that function asks
// IsFading about each hit, on the same thread.
using IsFadingFn = bool (*)(uint64_t entity, const void* view, int flag);
IsFadingFn g_is_fading = nullptr;
RegisterPair (*g_selected_character_changed)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) = nullptr;
uintptr_t g_picking_ray_site = 0;  // GameObjectPick's RaycastAll call (camera occluders)
thread_local picking::Ray t_picking_ray{};

// Whether GameObjectPick asks about the character right after its ray: not before the ray site, and
// within the same function.
bool AfterPickingRay(uintptr_t caller)
{
    return g_picking_ray_site && caller > g_picking_ray_site && caller - g_picking_ray_site < 0x1000;
}

bool PickingIsFadingHook(uint64_t entity, const void* view, int flag)
{
    const State& state = GetState();
    if (!entity || entity != state.active_character.load(std::memory_order_relaxed) ||
        !state.prevent_clicking_active_character || !state.IsCharacterMovementMode() || state.IsInControllerMode()) {
        return g_is_fading(entity, view, flag);
    }
    const auto caller = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
    const uint8_t* camera = state.camera.load(std::memory_order_relaxed);
    picking::Measure m{};
    if (camera && t_picking_ray.valid && AfterPickingRay(caller)) {
        m = picking::GroundDistanceBehind(camera, t_picking_ray);
    }
    const bool measured = m.valid;
    const float limit = state.max_click_distance_behind_character.load(std::memory_order_relaxed);
    const bool skip = !measured || !(m.behind > limit);

    // The decision, logged when it changes.
    static std::atomic<int> last_state{ -1 };
    const int decision = !measured ? 0 : skip ? 1 : 2;
    if (last_state.exchange(decision) != decision) {
        if (!measured) {
            LOG_DEBUG("Active character skipped by picking (no picking ray or camera to measure)");
        } else {
            LOG_DEBUG("Active character %s: the ray reaches the ground %.1f m behind it, limit %.1f "
                     "(camera %.1f m above the feet, camera root %.2f m above them)",
                skip ? "skipped by picking" : "kept clickable", m.behind, limit, t_picking_ray.start[1] - m.feet,
                m.offset);
        }
    }
    return skip ? true : g_is_fading(entity, view, flag);
}

// OnSelectedCharacterChanged(this, short player, const ecs::EntityRef* character, bool).
RegisterPair SelectedCharacterHook(uint64_t self, uint64_t player, uint64_t character, uint64_t flag, uint64_t a4,
    uint64_t a5)
{
    if (character) {
        const uint64_t handle = *reinterpret_cast<const uint64_t*>(character);
        if (GetState().active_character.exchange(handle) != handle) {
            LOG_DEBUG("Active character %#llx (player %d)", static_cast<unsigned long long>(handle),
                static_cast<int>(static_cast<int16_t>(player)));
        }
    }
    return g_selected_character_changed(self, player, character, flag, a4, a5);
}

void InstallUnclickableCharacter(const mem::MainExecutable& exe)
{
    auto file_address = [&](uintptr_t address) { return static_cast<unsigned long>(address - exe.load_bias); };
    UnclickableCharacterAddresses addresses;
    std::string error;
    if (!FindUnclickableCharacterAddresses(reinterpret_cast<const uint8_t*>(exe.code.start), exe.code.size,
            exe.code.start, addresses, error)) {
        LOG_WARN("The active character stays clickable: %s", error.c_str());
        return;
    }
    void* stub = mem::MakeJumpStub(exe.code.start, reinterpret_cast<const void*>(&PickingIsFadingHook));
    // push rbp ; push r15 ; push r14: whole instructions, nothing position-dependent.
    static constexpr uint8_t kSelectedEntry[] = { 0x55, 0x41, 0x57, 0x41, 0x56 };
    if (!stub || !mem::HookEntry(addresses.selected_character_changed, kSelectedEntry, sizeof kSelectedEntry,
                     reinterpret_cast<const void*>(&SelectedCharacterHook),
                     reinterpret_cast<void**>(&g_selected_character_changed))) {
        LOG_WARN("The active character stays clickable: could not hook OnSelectedCharacterChanged");
        return;
    }
    g_is_fading = reinterpret_cast<IsFadingFn>(addresses.is_fading);
    int redirected = 0;
    for (const uintptr_t site : addresses.picking_calls) {
        if (mem::RedirectCall(site, stub) == addresses.is_fading) {
            ++redirected;
        } else {
            LOG_WARN("Could not hook the IsFading call at %#lx", file_address(site));
        }
    }
    LOG_INFO("Unclickable active character: IsFading %#lx, %d of %zu picking calls, OnSelectedCharacterChanged %#lx",
        file_address(addresses.is_fading), redirected, addresses.picking_calls.size(),
        file_address(addresses.selected_character_changed));
}

// Camera occluders, like PreventClickingCameraOccludersCavehook on Windows: in Character mode the
// cursor ignores what stands between the camera and the character, such as trailing companions. The
// picking ray of GameObjectPick starts the camera's zoom distance further along, unless the camera
// looks down steeply or from far away. wasd_RaycastSiteStub replaces the ray's RaycastAll call and asks
// wasd_AdjustRayStart for the start; the camera comes from GetCameraPitchDegrees.
using CameraPitchFn = float (*)(const uint8_t* camera, bool a, bool b);
CameraPitchFn g_camera_pitch = nullptr;
// The return address of the camera update's call, the pitch the camera turns to (camera tweaks).
uintptr_t g_pitch_update_return = 0;

// Shared by the camera occluders and the camera tweaks. The entry hook jumps here, so the return
// address is the game's caller.
float CameraPitchHook(const uint8_t* camera, bool a, bool b)
{
    const auto caller = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
    // It runs for more than one camera, and the others may have a NaN or zero zoom and pitch. The
    // camera update's call (camera tweaks) is the one the player sees. Without that call found,
    // keep the last camera with sane values, like State::IsCameraValid on Windows.
    const bool update = g_pitch_update_return && caller == g_pitch_update_return;
    if (camera && (update || !g_pitch_update_return)) {
        float zoom, pitch;
        std::memcpy(&zoom, camera + 0x54, sizeof zoom);
        std::memcpy(&pitch, camera + 0x164, sizeof pitch);
        if (std::isfinite(zoom) && std::isfinite(pitch) && zoom > 0.01f) {
            GetState().camera.store(camera, std::memory_order_relaxed);
        }
    }
    const float pitch = g_camera_pitch(camera, a, b);
    if (update) {
        return camera_tweaks::OnUpdatePitch(const_cast<uint8_t*>(camera), pitch, camera::IsRotating(),
            GetState().IsInControllerMode());
    }
    return pitch;
}

// GetCameraPitchDegrees is hooked once, for whichever feature asks first.
bool HookCameraPitch(uintptr_t camera_pitch)
{
    static bool hooked = false;
    if (hooked) {
        return true;
    }
    // cmp byte [rdi+0x1E4], 0: one whole instruction, nothing position-dependent.
    static constexpr uint8_t kPitchEntry[] = { 0x80, 0xBF, 0xE4, 0x01, 0x00, 0x00, 0x00 };
    hooked = mem::HookEntry(camera_pitch, kPitchEntry, sizeof kPitchEntry,
        reinterpret_cast<const void*>(&CameraPitchHook), reinterpret_cast<void**>(&g_camera_pitch));
    return hooked;
}

void InstallCameraOccluders(const mem::MainExecutable& exe, const OccluderAddresses& addresses)
{
    auto file_address = [&](uintptr_t address) { return static_cast<unsigned long>(address - exe.load_bias); };
    if (!HookCameraPitch(addresses.camera_pitch)) {
        LOG_WARN("Camera occluders stay clickable: could not hook GetCameraPitchDegrees");
        return;
    }
    // `mov rcx, [rsp+0xB0] ; call [rax+0x48]` becomes `call stub ; nop6`. This runs before the game
    // starts, so no thread can be inside these bytes.
    void* stub = mem::MakeJumpStub(addresses.raycast_site, reinterpret_cast<const void*>(&wasd_RaycastSiteStub));
    if (!stub) {
        LOG_WARN("Camera occluders stay clickable: no jump stub");
        return;
    }
    uint8_t code[11] = { 0xE8, 0, 0, 0, 0, 0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00 };
    const int32_t rel = static_cast<int32_t>(reinterpret_cast<intptr_t>(stub) -
                                             static_cast<intptr_t>(addresses.raycast_site + 5));
    std::memcpy(code + 1, &rel, sizeof rel);
    if (!mem::WriteCode(addresses.raycast_site, code, sizeof code)) {
        LOG_WARN("Camera occluders stay clickable: could not patch the picking ray");
        return;
    }
    g_picking_ray_site = addresses.raycast_site;
    LOG_INFO("Camera occluders: GetCameraPitchDegrees %#lx, picking ray %#lx, %s",
        file_address(addresses.camera_pitch), file_address(addresses.raycast_site),
        GetState().prevent_clicking_camera_occluders ? "on" : "off in the config");

    // Walk-back protection: `mov dword [rsp], 0x194280 ; call rax` becomes `call stub ; nop4`.
    void* walkable_stub = addresses.walkable_ray_site
        ? mem::MakeJumpStub(addresses.walkable_ray_site, reinterpret_cast<const void*>(&wasd_WalkableRaySiteStub))
        : nullptr;
    uint8_t walkable[9] = { 0xE8, 0, 0, 0, 0, 0x0F, 0x1F, 0x40, 0x00 };
    if (walkable_stub) {
        const int32_t walkable_rel = static_cast<int32_t>(reinterpret_cast<intptr_t>(walkable_stub) -
                                                          static_cast<intptr_t>(addresses.walkable_ray_site + 5));
        std::memcpy(walkable + 1, &walkable_rel, sizeof walkable_rel);
    }
    if (!walkable_stub || !mem::WriteCode(addresses.walkable_ray_site, walkable, sizeof walkable)) {
        LOG_WARN("Walk-back protection off: the walkable ray %s",
            addresses.walkable_ray_site ? "could not be patched" : "was not found");
        return;
    }
    LOG_INFO("Walk-back protection: walkable ray %#lx", file_address(addresses.walkable_ray_site));
}

// Camera tweaks ported from Native Camera Tweaks by ersh1 (camera_tweaks.cpp): the camera update's
// pitch comes from CameraPitchHook, and the game's camera definitions are found through the same
// code.
void InstallCameraTweaks(const mem::MainExecutable& exe, const CameraTweakAddresses& addresses)
{
    auto file_address = [&](uintptr_t address) { return static_cast<unsigned long>(address - exe.load_bias); };
    bool pointer_in_data = false;
    for (int i = 0; i < exe.data_count; ++i) {
        pointer_in_data |= exe.data[i].Contains(addresses.settings_pointer);
    }
    if (!pointer_in_data) {
        LOG_WARN("Camera tweaks off: the camera settings pointer is not inside the game's data segments");
        return;
    }
    camera_tweaks::GameLayout layout;
    layout.settings = reinterpret_cast<uint8_t* const*>(addresses.settings_pointer);
    layout.override_flag = addresses.override_flag;
    layout.exploration = addresses.exploration;
    layout.combat = addresses.combat;
    layout.override_definition = addresses.override_definition;
    camera_tweaks::SetGameLayout(layout);
    if (!HookCameraPitch(addresses.camera_pitch)) {
        LOG_WARN("Camera tweaks off: could not hook GetCameraPitchDegrees");
        return;
    }
    g_pitch_update_return = addresses.pitch_return;
    LOG_INFO("Camera tweaks: settings %#lx (definitions +%#x, +%#x, +%#x), pitch update %#lx",
        file_address(addresses.settings_pointer), addresses.exploration, addresses.combat,
        addresses.override_definition, file_address(addresses.pitch_return - 5));
}

// Zoom and keyboard rotation speed (camera tweaks): the stores where ExecuteCameraCommand writes the
// speed into the camera become calls of wasd_ZoomSpeedStub and wasd_RotationSpeedStub. The game
// picks the camera, so the speed lands in the right one.
void InstallCameraSpeedStores(const mem::MainExecutable& exe, const CameraSpeedStores& stores)
{
    auto file_address = [&](uintptr_t address) { return static_cast<unsigned long>(address - exe.load_bias); };
    // `call stub ; nop4` replaces the 9-byte movss. This runs before the game starts, so no thread
    // can be inside these bytes.
    auto patch = [](uintptr_t site, void (*target)()) {
        void* stub = mem::MakeJumpStub(site, reinterpret_cast<const void*>(target));
        if (!stub) {
            return false;
        }
        uint8_t code[9] = { 0xE8, 0, 0, 0, 0, 0x0F, 0x1F, 0x40, 0x00 };
        const int32_t rel = static_cast<int32_t>(reinterpret_cast<intptr_t>(stub) - static_cast<intptr_t>(site + 5));
        std::memcpy(code + 1, &rel, sizeof rel);
        return mem::WriteCode(site, code, sizeof code);
    };
    if (!patch(stores.zoom, &wasd_ZoomSpeedStub) || !patch(stores.rotation, &wasd_RotationSpeedStub)) {
        LOG_WARN("Camera tweaks: the game's zoom and rotation speeds stay, could not patch the speed stores");
        return;
    }
    LOG_INFO("Camera tweaks: zoom speed store %#lx, rotation speed store %#lx", file_address(stores.zoom),
        file_address(stores.rotation));
}

// The occluders and the camera tweaks both hook the entry of GetCameraPitchDegrees, and the
// signatures of both check that entry, so both searches run on the unpatched code first.
void InstallCameraFeatures(const mem::MainExecutable& exe, uintptr_t execute_camera_command)
{
    const auto* code = reinterpret_cast<const uint8_t*>(exe.code.start);
    OccluderAddresses occluders;
    CameraTweakAddresses tweaks;
    std::string occluder_error, tweak_error;
    const bool occluders_found =
        FindOccluderAddresses(code, exe.code.size, exe.code.start, occluders, occluder_error);
    const bool tweaks_found = FindCameraTweakAddresses(code, exe.code.size, exe.code.start, tweaks, tweak_error);
    if (occluders_found) {
        InstallCameraOccluders(exe, occluders);
    } else {
        LOG_WARN("Camera occluders stay clickable: %s", occluder_error.c_str());
    }
    if (tweaks_found) {
        InstallCameraTweaks(exe, tweaks);
    } else {
        LOG_WARN("Camera tweaks off: %s", tweak_error.c_str());
    }
    if (!execute_camera_command) {
        return;
    }
    CameraSpeedStores stores;
    std::string error;
    if (!FindCameraSpeedStores(code, exe.code.size, exe.code.start, execute_camera_command, stores, error)) {
        LOG_WARN("Camera tweaks: the game's zoom and rotation speeds stay, %s", error.c_str());
        return;
    }
    InstallCameraSpeedStores(exe, stores);
}

// Runs in every process that inherits LD_PRELOAD (Steam's launch wrappers, the crash handler, ...),
// so it does nothing unless this is the game itself.
__attribute__((constructor)) void OnLibraryLoad()
{
    if (!IsGameProcess()) {
        return;
    }
    const std::string dir = LibraryDirectory();
    log::Open(LogPath().c_str());
    LOG_INFO("BG3 WASD Everywhere %s for Linux, pid %d", BG3WASD_VERSION, static_cast<int>(getpid()));

    SetSettingsPath(dir + "/" + kFileStem + ".toml");
    ReloadSettings();
    WatchSettingsFile();

    if (!Install()) {
        LOG_ERROR("Mod disabled: unsupported game build or patch failure");
        return;
    }
    // The game hasn't read its key bindings yet, so they can be patched on disk now.
    const inputconfig::Result bindings = inputconfig::PatchAllProfiles();
    if (bindings.ok) {
        input::SetMovementKeys(bindings.forward_keys, bindings.backward_keys);
        GetState().rotate_keys_include_lmb = bindings.rotate_includes_left_click;
    }
    // Mouselook holds down the first plain binding of CameraToggleMouseRotate (no modifiers).
    std::string rotate_input = "mouse:middle";  // the game's default
    for (const std::string& key : bindings.rotate_keys) {
        if (key.find('+') == std::string::npos && key != "mouse:left") {
            rotate_input = key;
            break;
        }
    }
    bool rotate_is_mouse_button = false;
    int rotate_code = 0;
    if (input::LookupKey(rotate_input, rotate_is_mouse_button, rotate_code)) {
        camera::SetRotateInput(rotate_is_mouse_button, rotate_code);
        LOG_INFO("Mouselook holds %s", rotate_input.c_str());
    } else {
        LOG_WARN("Mouselook can't hold \"%s\"; using the middle mouse button", rotate_input.c_str());
    }
    input::Enable();
    LOG_INFO("WASD movement enabled");
}

}  // namespace

State& GetState()
{
    static State state;
    return state;
}

void State::SetMovementModeToggled(bool value)
{
    movement_mode_toggled = value;
    OnMovementModeChanged(*this);
}

void State::SetMovementModeHeld(bool value)
{
    movement_mode_held = value;
    OnMovementModeChanged(*this);
}

}  // namespace wasd

// Called by wasd_RaycastSiteStub on a picking worker thread with RaycastAll's start and end. Returns
// the start to use: a moved copy, or the game's own.
extern "C" __attribute__((visibility("hidden"), used, noinline, noipa)) const float* wasd_AdjustRayStart(
    const float* start, const float* end)
{
    using namespace wasd;
    // For the active character's click distance (PickingIsFadingHook), with the game's own start.
    std::memcpy(t_picking_ray.start, start, sizeof t_picking_ray.start);
    std::memcpy(t_picking_ray.end, end, sizeof t_picking_ray.end);
    t_picking_ray.valid = true;
    const State& state = GetState();
    if (!state.prevent_clicking_camera_occluders || !state.IsCharacterMovementMode() || state.IsInControllerMode()) {
        return start;
    }
    const uint8_t* camera = state.camera.load(std::memory_order_relaxed);
    float zoom = 0.0f, pitch = 0.0f;
    if (camera) {
        std::memcpy(&zoom, camera + 0x54, sizeof zoom);    // Windows Camera::currentZoomA
        std::memcpy(&pitch, camera + 0x164, sizeof pitch);  // Windows Camera::currentPitch_164
    }
    // The Windows mod's exception: close up, a steep angle is needed before objects get skipped.
    constexpr float kThreshold = 300.0f;
    const float d[3] = { end[0] - start[0], end[1] - start[1], end[2] - start[2] };
    const float length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    const bool move = zoom > 0.0f && zoom * pitch < kThreshold && length > zoom;

    // The decision and the camera values, logged when the decision changes.
    static std::atomic<int> last_state{ -1 };
    const int decision = !camera ? 0 : move ? 1 : 2;
    if (last_state.exchange(decision) != decision) {
        LOG_DEBUG("Picking ray %s (zoom %.1f, pitch %.1f, ray %.1f)",
            !camera ? "kept: no camera yet" : move ? "moved past camera occluders" : "kept: camera exception",
            zoom, pitch, length);
    }
    if (!move) {
        return start;
    }
    thread_local float moved[3];
    for (int i = 0; i < 3; ++i) {
        moved[i] = start[i] + d[i] / length * zoom;
    }
    return moved;
}

// Called by wasd_WalkableRaySiteStub on a picking worker thread with the walkable ray's start and end
// (walk-back protection, picking.hpp). Returns the start to use.
extern "C" __attribute__((visibility("hidden"), used, noinline, noipa)) const float* wasd_AdjustWalkableRayStart(
    const float* start, const float* end)
{
    using namespace wasd;
    const State& state = GetState();
    if (!state.prevent_clicking_camera_occluders || !state.IsCharacterMovementMode() || state.IsInControllerMode()) {
        return start;
    }
    thread_local float moved[3];
    const picking::WalkableRay result =
        picking::WalkableRayStart(state.camera.load(std::memory_order_relaxed), start, end,
            state.max_click_distance_toward_camera.load(std::memory_order_relaxed), moved);

    // The decision, logged when it changes.
    static std::atomic<int> last_state{ -1 };
    if (last_state.exchange(static_cast<int>(result)) != static_cast<int>(result)) {
        const char* what = "moved past the ground between the camera and the character";
        if (result == picking::WalkableRay::Cancelled) {
            what = "cancelled: the cursor is on the ground between the camera and the character";
        } else if (result == picking::WalkableRay::NoCamera) {
            what = "kept: no camera yet";
        } else if (result == picking::WalkableRay::CameraException) {
            what = "kept: camera exception";
        } else if (result == picking::WalkableRay::NotTowardCharacter) {
            what = "kept: it doesn't head toward the character";
        }
        LOG_DEBUG("Walkable ray %s", what);
    }
    const bool changed = result == picking::WalkableRay::Moved || result == picking::WalkableRay::Cancelled;
    return changed ? moved : start;
}

// Replaces `mov rcx, [rsp+0xB0] ; call [rax+0x48]` in GameObjectPick, entered with `call`: does the
// mov (0xB8 past the return address), swaps rsi for wasd_AdjustRayStart's answer and jumps to
// RaycastAll, which returns to the game. The argument registers are kept; seven pushes after the
// return address align the stack for the call.
asm(R"(
    .pushsection .text
    .p2align 4
    .globl wasd_RaycastSiteStub
    .hidden wasd_RaycastSiteStub
    .type wasd_RaycastSiteStub, @function
wasd_RaycastSiteStub:
    movq 0xb8(%rsp), %rcx
    pushq %rax
    pushq %rdi
    pushq %rsi
    pushq %rdx
    pushq %rcx
    pushq %r8
    pushq %r9
    movq %rsi, %rdi
    movq %rdx, %rsi
    call wasd_AdjustRayStart
    movq %rax, %r11
    popq %r9
    popq %r8
    popq %rcx
    popq %rdx
    popq %rsi
    popq %rdi
    popq %rax
    movq %r11, %rsi
    jmpq *0x48(%rax)
    .size wasd_RaycastSiteStub, .-wasd_RaycastSiteStub
    .popsection
)");

// Replaces `mov dword [rsp], 0x194280 ; call rax` in WalkablePick, entered with `call`: stores the
// mask (8 past the return address), swaps rsi for wasd_AdjustWalkableRayStart's answer and jumps to
// RaycastAll, which returns to the game. As in wasd_RaycastSiteStub, seven pushes align the stack.
asm(R"(
    .pushsection .text
    .p2align 4
    .globl wasd_WalkableRaySiteStub
    .hidden wasd_WalkableRaySiteStub
    .type wasd_WalkableRaySiteStub, @function
wasd_WalkableRaySiteStub:
    movl $0x194280, 0x8(%rsp)
    pushq %rax
    pushq %rdi
    pushq %rsi
    pushq %rdx
    pushq %rcx
    pushq %r8
    pushq %r9
    movq %rsi, %rdi
    movq %rdx, %rsi
    call wasd_AdjustWalkableRayStart
    movq %rax, %r11
    popq %r9
    popq %r8
    popq %rcx
    popq %rdx
    popq %rsi
    popq %rdi
    popq %rax
    movq %r11, %rsi
    jmpq *%rax
    .size wasd_WalkableRaySiteStub, .-wasd_WalkableRaySiteStub
    .popsection
)");

// Replace `movss [r8+0xA4], xmm0` (zoom speed) and `movss [r8+0x9C], xmm1` (keyboard rotation speed)
// in ExecuteCameraCommand, entered with `call`: store the value times the multiplier. The register
// and the flags are left as they were; the register is kept below the return address meanwhile
// (the red zone of this leaf code).
asm(R"(
    .pushsection .text
    .p2align 4
    .globl wasd_ZoomSpeedStub
    .hidden wasd_ZoomSpeedStub
    .type wasd_ZoomSpeedStub, @function
wasd_ZoomSpeedStub:
    movups %xmm0, -24(%rsp)
    mulss wasd_zoom_speed_mult(%rip), %xmm0
    movss %xmm0, 0xa4(%r8)
    movups -24(%rsp), %xmm0
    ret
    .size wasd_ZoomSpeedStub, .-wasd_ZoomSpeedStub

    .p2align 4
    .globl wasd_RotationSpeedStub
    .hidden wasd_RotationSpeedStub
    .type wasd_RotationSpeedStub, @function
wasd_RotationSpeedStub:
    movups %xmm1, -24(%rsp)
    mulss wasd_rotation_speed_mult(%rip), %xmm1
    movss %xmm1, 0x9c(%r8)
    movups -24(%rsp), %xmm1
    ret
    .size wasd_RotationSpeedStub, .-wasd_RotationSpeedStub
    .popsection
)");
