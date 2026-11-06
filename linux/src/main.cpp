// BG3WASD for the native Linux build of Baldur's Gate 3, loaded with LD_PRELOAD.
//
// Prototype scope: direct WASD character movement (unlock + movement input override) and the
// movement hotkeys. See linux/README.md.
#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>

#include <climits>
#include <cstdlib>
#include <cstring>
#include <string>

#include "camera.hpp"
#include "input.hpp"
#include "log.hpp"
#include "memory.hpp"
#include "settings.hpp"
#include "signatures.hpp"
#include "state.hpp"

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
        state.IsCharacterMovementMode() && !state.IsInControllerMode()) {
        *command_id = 0;
    }
    return g_execute_camera_command(self, context, command, a3, a4, a5);
}

void OnMovementModeChanged(State& state)
{
    const bool character = state.IsCharacterMovementMode();
    if (character) {
        state.frames_to_center_camera = kFramesToCenterCamera;
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

// ${XDG_STATE_HOME:-~/.local/state}/BG3WASD/BG3WASD.log. Not next to the library: inside the
// Steam Linux Runtime container, the directory of an LD_PRELOAD library is mounted read-only.
std::string LogPath()
{
    std::string dir;
    if (const char* state = std::getenv("XDG_STATE_HOME"); state && *state) {
        dir = state;
    } else if (const char* home = std::getenv("HOME"); home && *home) {
        dir = std::string(home) + "/.local/state";
    } else {
        return "/tmp/BG3WASD.log";
    }
    dir += "/BG3WASD";
    for (size_t slash = dir.find('/', 1); slash != std::string::npos; slash = dir.find('/', slash + 1)) {
        mkdir(dir.substr(0, slash).c_str(), 0755);
    }
    mkdir(dir.c_str(), 0755);
    return dir + "/BG3WASD.log";
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
    return true;
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
    LOG_INFO("BG3WASD Linux prototype %s, pid %d", BG3WASD_VERSION, static_cast<int>(getpid()));

    SetSettingsPath(dir + "/BG3WASD.toml");
    ReloadSettings();

    if (!Install()) {
        LOG_ERROR("Mod disabled: unsupported game build or patch failure");
        return;
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
