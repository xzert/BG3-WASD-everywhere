#include "camera.hpp"

#include <dlfcn.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <vector>

#include "log.hpp"
#include "signatures.hpp"
#include "state.hpp"

namespace wasd::camera {
namespace {

constexpr uint64_t kRotationCursorId = 0x2E;
constexpr int kOverrideDistanceSquared = 6 * 6;  // mouse travel before a right-click counts as rotation
constexpr uint32_t kSdlMouseMotion = 0x400;
constexpr size_t kSdlEventSize = 56;

// The game's own SDL2 (libSDL2.so in the game directory), resolved at runtime.
struct Sdl {
    int (*SetRelativeMouseMode)(int) = nullptr;
    void (*WarpMouseInWindow)(void*, int, int) = nullptr;
    uint32_t (*GetMouseState)(int*, int*) = nullptr;
    void (*GetWindowSize)(void*, int*, int*) = nullptr;
    int (*SetHint)(const char*, const char*) = nullptr;
    int (*PushEvent)(void*) = nullptr;
    uint32_t (*GetTicks)() = nullptr;
    void* (*GetKeyboardFocus)() = nullptr;
    uint32_t (*GetWindowID)(void*) = nullptr;
    int32_t (*GetKeyFromScancode)(int32_t) = nullptr;
    int (*IsTextInputActive)() = nullptr;

    bool Load()
    {
        auto load = [](auto& fn, const char* name) {
            fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(dlsym(RTLD_NEXT, name));
            return fn != nullptr;
        };
        return load(SetRelativeMouseMode, "SDL_SetRelativeMouseMode") &
               load(WarpMouseInWindow, "SDL_WarpMouseInWindow") & load(GetMouseState, "SDL_GetMouseState") &
               load(GetWindowSize, "SDL_GetWindowSize") & load(SetHint, "SDL_SetHint") &
               load(PushEvent, "SDL_PushEvent") & load(GetTicks, "SDL_GetTicks") &
               load(GetKeyboardFocus, "SDL_GetKeyboardFocus") & load(GetWindowID, "SDL_GetWindowID") &
               load(GetKeyFromScancode, "SDL_GetKeyFromScancode") &
               load(IsTextInputActive, "SDL_IsTextInputActive");
    }
} g_sdl;

enum class VirtualCursor { Normal, RequestOverride, Override, EndOverride };

struct Point {
    int x = -1, y = -1;  // x == -1: nothing to restore yet (game start)
};

struct Input {
    bool mouse_button = false;
    int code = -1;

    bool operator==(const Input&) const = default;
};

// Shared by the game thread (rotation start/stop) and the main thread (SDL event loop).
struct RotationState {
    std::mutex mutex;
    bool rotating = false;
    bool should_override_cursor = false;
    bool overridden_last_frame = false;
    VirtualCursor virtual_cursor = VirtualCursor::Normal;
    Point restore;
    Point virtual_position{ 0, 0 };
    Input last_press;  // most recent key or mouse button press
    Input trigger;     // the press that started the current rotation
} g_rotation;

std::atomic<bool> g_rotating{ false };  // mirrors g_rotation.rotating for lock-free reads
std::atomic<RotationCursor> g_cursor_character{ RotationCursor::Invisible };
std::atomic<RotationCursor> g_cursor_camera{ RotationCursor::Invisible };
std::atomic<void*> g_window{ nullptr };
bool g_installed = false;

struct RegisterPair {
    uint64_t rax, rdx;
};
using CursorFn = RegisterPair (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
using Vec4 = float __attribute__((vector_size(16)));
// SetVirtualCursorPos(this in rdi, const float2* mouse in rsi) returns the position in xmm0. xmm0
// also holds the mouse position at the call, so it is forwarded as well.
using SetVirtualCursorPosFn = Vec4 (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, Vec4);

CursorFn g_cursor_push = nullptr;
CursorFn g_cursor_pop = nullptr;
uint32_t g_virtual_cursor_slot = 0;

RotationCursor CurrentRotationCursor()
{
    return GetState().IsCharacterMovementMode() ? g_cursor_character.load() : g_cursor_camera.load();
}

void* Window()
{
    void* window = g_window;
    return window ? window : g_sdl.GetKeyboardFocus();
}

void WindowSize(int& w, int& h)
{
    w = h = 0;
    if (void* window = Window()) {
        g_sdl.GetWindowSize(window, &w, &h);
    }
}

// An empty mouse motion event makes the game refresh its cursor right away.
void PushMouseMotion()
{
    uint8_t event[kSdlEventSize] = {};
    const uint32_t timestamp = g_sdl.GetTicks();
    std::memcpy(event, &kSdlMouseMotion, sizeof kSdlMouseMotion);
    std::memcpy(event + 4, &timestamp, sizeof timestamp);
    g_sdl.PushEvent(event);
}

// CameraRotationOnHook
void OnRotationStart()
{
    std::lock_guard lock(g_rotation.mutex);
    const bool was_rotating = g_rotation.rotating;
    g_rotation.rotating = true;
    g_rotating = true;

    const RotationCursor type = CurrentRotationCursor();
    if (type == RotationCursor::Invisible) {
        // Not centering avoids the cursor ending up in odd places when Rotate is spam-clicked.
        g_sdl.SetHint("SDL_MOUSE_RELATIVE_MODE_CENTER", "0");
        g_sdl.SetHint("SDL_MOUSE_RELATIVE_CURSOR_VISIBLE", "0");
        if (!was_rotating) {
            g_sdl.GetMouseState(&g_rotation.restore.x, &g_rotation.restore.y);
        }
    } else if (type == RotationCursor::Centered) {
        g_sdl.SetHint("SDL_MOUSE_RELATIVE_MODE_CENTER", "1");
        g_sdl.SetHint("SDL_MOUSE_RELATIVE_CURSOR_VISIBLE", "1");
        if (!was_rotating) {
            int w, h;
            WindowSize(w, h);
            g_rotation.restore = { w / 2, h / 2 };
        }
    }
    if (type != RotationCursor::Vanilla) {
        g_rotation.should_override_cursor = true;
    }
    if (!was_rotating) {
        // Rotation starts right after its key or button press reaches the game.
        g_rotation.trigger = g_rotation.last_press;
        LOG_INFO("Rotation start (%s %d), cursor saved at %d,%d", g_rotation.trigger.mouse_button ? "mouse" : "key",
            g_rotation.trigger.code, g_rotation.restore.x, g_rotation.restore.y);
    }
}

// Caller holds g_rotation.mutex.
void EndRotation(const char* reason)
{
    if (!g_rotation.rotating) {
        return;
    }
    g_rotation.rotating = false;
    g_rotating = false;
    if (g_rotation.restore.x != -1) {  // skip the pop the game does on startup
        g_rotation.should_override_cursor = false;
    }
    LOG_INFO("Rotation end: %s", reason);
}

// CameraRotationOffHook
void OnRotationStop()
{
    std::lock_guard lock(g_rotation.mutex);
    EndRotation("rotation cursor popped");
}

RegisterPair CursorPushHook(uint64_t manager, uint64_t cursor, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    if (static_cast<uint32_t>(cursor) == kRotationCursorId && !GetState().IsInControllerMode()) {
        OnRotationStart();
    }
    return g_cursor_push(manager, cursor, a2, a3, a4, a5);
}

RegisterPair CursorPopHook(uint64_t manager, uint64_t cursor, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    if (static_cast<uint32_t>(cursor) == kRotationCursorId) {
        OnRotationStop();
    }
    return g_cursor_pop(manager, cursor, a2, a3, a4, a5);
}

// SetVirtualCursorPosHook: while rotating, the game's virtual cursor is moved off-screen
// (invisible) or to the center (centered), so nothing under the real cursor gets hovered or clicked.
Vec4 SetVirtualCursorPosHook(uint64_t self, uint64_t mouse, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, Vec4 xmm0)
{
    const auto vtable = *reinterpret_cast<const uintptr_t*>(self);
    const auto original = *reinterpret_cast<const SetVirtualCursorPosFn*>(vtable + g_virtual_cursor_slot);
    Vec4 position = original(self, mouse, a2, a3, a4, a5, xmm0);

    std::lock_guard lock(g_rotation.mutex);
    g_rotation.virtual_position = { static_cast<int>(position[0]), static_cast<int>(position[1]) };
    if (g_rotation.virtual_cursor == VirtualCursor::Override) {
        int w, h;
        WindowSize(w, h);
        const RotationCursor type = CurrentRotationCursor();
        if (type == RotationCursor::Invisible) {
            position[0] = static_cast<float>(w / 2);
            position[1] = -1000000.0f;
        } else if (type == RotationCursor::Centered) {
            position[0] = static_cast<float>(w / 2);
            position[1] = static_cast<float>(h / 2);
        }
    } else if (g_rotation.virtual_cursor == VirtualCursor::EndOverride) {
        position[0] = static_cast<float>(g_rotation.restore.x);
        position[1] = static_cast<float>(g_rotation.restore.y);
        g_rotation.virtual_cursor = VirtualCursor::Normal;
    }
    return position;
}

// Mouselook (ToggleCameraRotation). The Windows mod sends the rotate command to the camera's command
// handler; this port pushes the rotate input (CameraToggleMouseRotate's binding) as an SDL event and
// holds it, so the game's own input path starts the rotation.
constexpr uint32_t kSdlKeyDownEvent = 0x300;
constexpr uint32_t kSdlKeyUpEvent = 0x301;
constexpr uint32_t kSdlMouseButtonDownEvent = 0x401;
constexpr uint32_t kSdlMouseButtonUpEvent = 0x402;
constexpr int kFramesToStartRotating = 3;
constexpr int kMaxPressAttempts = 3;  // presses that don't start rotation before mouselook gives up

std::atomic<bool> g_mouselook_on{ false };
std::atomic<bool> g_in_dialog{ false };
Input g_rotate_input{ true, 2 };  // the middle mouse button, the game's default
// Main thread only:
// Panels opened with their keys while mouselook was on. A function-local static: the constructor in
// main.cpp that starts the mod can run before this file's static initializers.
std::vector<int>& OpenPanels()
{
    static std::vector<int> panels;
    return panels;
}
bool g_rotate_input_down = false;
int g_frames_since_press = 0;
int g_press_attempts = 0;

void PushRotateInput(bool down)
{
    uint8_t event[kSdlEventSize] = {};
    const uint32_t timestamp = g_sdl.GetTicks();
    void* window = Window();
    const uint32_t window_id = window ? g_sdl.GetWindowID(window) : 0;
    std::memcpy(event + 4, &timestamp, sizeof timestamp);
    std::memcpy(event + 8, &window_id, sizeof window_id);
    if (g_rotate_input.mouse_button) {
        const uint32_t type = down ? kSdlMouseButtonDownEvent : kSdlMouseButtonUpEvent;
        int x = 0, y = 0;
        g_sdl.GetMouseState(&x, &y);
        std::memcpy(event, &type, sizeof type);
        event[16] = static_cast<uint8_t>(g_rotate_input.code);  // button
        event[17] = down ? 1 : 0;                               // state
        event[18] = 1;                                          // clicks
        std::memcpy(event + 20, &x, sizeof x);
        std::memcpy(event + 24, &y, sizeof y);
    } else {
        const uint32_t type = down ? kSdlKeyDownEvent : kSdlKeyUpEvent;
        const int32_t scancode = g_rotate_input.code;
        const int32_t keycode = g_sdl.GetKeyFromScancode(scancode);
        std::memcpy(event, &type, sizeof type);
        event[12] = down ? 1 : 0;  // state
        std::memcpy(event + 16, &scancode, sizeof scancode);
        std::memcpy(event + 20, &keycode, sizeof keycode);
    }
    g_sdl.PushEvent(event);
}

// The game asks for the movement input every frame while the player can move freely, and stops in
// menus, the pause menu, dialogs, loading screens and when the window loses focus. The Windows mod
// tracks each of those with its own hooks; on Linux this one signal covers them.
constexpr int64_t kNoMovementPollMs = 250;

const char* MouselookPauseReason()
{
    if (g_in_dialog) {
        return "dialog";
    }
    if (!OpenPanels().empty()) {
        return "a panel is open";
    }
    if (g_sdl.IsTextInputActive()) {
        return "text input";
    }
    if (NowMs() - GetState().last_movement_poll_ms > kNoMovementPollMs) {
        return "the game isn't taking movement input (menu, pause, loading)";
    }
    return nullptr;
}

// Once per frame, main thread.
void UpdateMouselook()
{
    if (!g_mouselook_on) {
        OpenPanels().clear();
    }
    static const char* last_reason = nullptr;
    const char* reason = g_mouselook_on ? MouselookPauseReason() : nullptr;
    if (reason != last_reason) {
        if (reason) {
            LOG_INFO("Mouselook paused: %s", reason);
        } else if (g_mouselook_on) {
            LOG_INFO("Mouselook resumed");
        }
        last_reason = reason;
    }
    const bool want = g_mouselook_on && !reason;
    if (!want) {
        if (g_rotate_input_down) {
            PushRotateInput(false);
            g_rotate_input_down = false;
        }
        g_press_attempts = 0;
        return;
    }
    if (g_rotating) {
        g_press_attempts = 0;
        return;
    }
    // Not rotating: the press hasn't reached the camera yet, the UI took it, or the player pressed and
    // released the rotate input. Release, then press again, a few times at most.
    if (g_rotate_input_down) {
        if (++g_frames_since_press >= kFramesToStartRotating) {
            PushRotateInput(false);
            g_rotate_input_down = false;
        }
    } else if (g_press_attempts < kMaxPressAttempts) {
        PushRotateInput(true);
        g_rotate_input_down = true;
        g_frames_since_press = 0;
        ++g_press_attempts;
    } else {
        g_mouselook_on = false;
        LOG_WARN("Mouselook off: the game didn't start rotating after %d presses", kMaxPressAttempts);
    }
}

// Character mode camera.
constexpr uint16_t kJneOpcode = 0x850F;    // bytes 0F 85: jne rel32
constexpr uint16_t kNopJmpOpcode = 0xE990;  // bytes 90 E9: nop ; jmp rel32, same target

std::mutex g_edge_panning_mutex;
uintptr_t g_edge_panning_jne = 0;
bool g_edge_panning_blocked = false;
CursorFn g_bounds_test = nullptr;

bool CharacterModeCameraActive()
{
    const State& state = GetState();
    return state.IsCharacterMovementMode() && !state.in_combat;
}

// PreventLosingChaseCamTargetHook: report "not inside" in Character mode, like the Windows mod.
RegisterPair BoundsTestHook(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    if (CharacterModeCameraActive()) {
        return { 0, 0 };
    }
    return g_bounds_test(a0, a1, a2, a3, a4, a5);
}

}  // namespace

void UpdateCharacterModeCamera()
{
    std::lock_guard lock(g_edge_panning_mutex);
    if (!g_edge_panning_jne) {
        return;
    }
    const bool block = CharacterModeCameraActive();
    if (block == g_edge_panning_blocked) {
        return;
    }
    if (!mem::WriteCodeAtomic16(g_edge_panning_jne, block ? kNopJmpOpcode : kJneOpcode)) {
        LOG_WARN("Could not %s edge panning: %s", block ? "block" : "restore", std::strerror(errno));
        return;
    }
    uint16_t now;
    std::memcpy(&now, reinterpret_cast<const void*>(g_edge_panning_jne), sizeof now);
    g_edge_panning_blocked = block;
    LOG_INFO("Edge panning %s (code now %02x %02x)", block ? "blocked" : "allowed", now & 0xFF, now >> 8);
}

bool InstallCharacterModeCamera(const mem::MainExecutable& exe, uintptr_t is_in_controller_mode)
{
    auto file_address = [&](uintptr_t address) { return static_cast<unsigned long>(address - exe.load_bias); };
    CharacterCameraAddresses addresses;
    std::string error;
    if (!FindCharacterCameraAddresses(reinterpret_cast<const uint8_t*>(exe.code.start), exe.code.size,
            exe.code.start, is_in_controller_mode, addresses, error)) {
        LOG_WARN("Edge panning and chase camera unchanged in Character mode: %s", error.c_str());
        return false;
    }
    uint16_t opcode;
    std::memcpy(&opcode, reinterpret_cast<const void*>(addresses.edge_panning_jne), sizeof opcode);
    void* stub = mem::MakeJumpStub(exe.code.start, reinterpret_cast<const void*>(&BoundsTestHook));
    if (opcode != kJneOpcode || (addresses.edge_panning_jne & 63) == 63 || !stub) {
        LOG_WARN("Edge panning and chase camera unchanged in Character mode: unexpected code or no jump stub");
        return false;
    }

    int32_t rel;
    std::memcpy(&rel, reinterpret_cast<const void*>(addresses.bounds_call + 1), sizeof rel);
    g_bounds_test = reinterpret_cast<CursorFn>(addresses.bounds_call + 5 + static_cast<int64_t>(rel));
    if (!mem::RedirectCall(addresses.bounds_call, stub)) {
        LOG_WARN("Chase camera unchanged in Character mode: could not hook the bounds test");
    }
    {
        std::lock_guard lock(g_edge_panning_mutex);
        g_edge_panning_jne = addresses.edge_panning_jne;
    }
    UpdateCharacterModeCamera();
    LOG_INFO("Edge panning check %#lx, chase camera bounds call %#lx", file_address(addresses.edge_panning_jne),
        file_address(addresses.bounds_call));
    return true;
}

RotationCursor ParseRotationCursor(const std::string& name)
{
    if (name == "centered") {
        return RotationCursor::Centered;
    }
    if (name == "vanilla") {
        return RotationCursor::Vanilla;
    }
    if (name != "invisible") {
        LOG_WARN("Unknown rotation cursor \"%s\", using invisible", name.c_str());
    }
    return RotationCursor::Invisible;
}

void SetRotationCursors(RotationCursor character_mode, RotationCursor camera_mode)
{
    g_cursor_character = character_mode;
    g_cursor_camera = camera_mode;
}

void SetWindow(void* window) { g_window = window; }

bool IsRotating() { return g_rotating; }

void SetMouselook(bool on)
{
    if (g_mouselook_on.exchange(on) != on) {
        LOG_INFO("Mouselook %s", on ? "on" : "off");
    }
}

void ToggleMouselook()
{
    if (g_mouselook_on && !OpenPanels().empty()) {
        OpenPanels().clear();  // paused for a panel: the key resumes instead of turning it off
        return;
    }
    SetMouselook(!g_mouselook_on);
}

void OnPanelKey(int key)
{
    if (!g_mouselook_on) {
        return;
    }
    std::vector<int>& panels = OpenPanels();
    const auto open = std::find(panels.begin(), panels.end(), key);
    if (open != panels.end()) {
        panels.erase(open);
    } else {
        panels.push_back(key);
    }
}

void OnCloseAllPanels() { OpenPanels().clear(); }

void SetRotateInput(bool mouse_button, int code) { g_rotate_input = { mouse_button, code }; }

void SetDialogActive(bool active) { g_in_dialog = active; }

void OnInputEvent(bool mouse_button, int code, bool down)
{
    if (!g_installed) {
        return;
    }
    std::lock_guard lock(g_rotation.mutex);
    const Input input{ mouse_button, code };
    if (down) {
        g_rotation.last_press = input;
    } else if (g_rotation.rotating && input == g_rotation.trigger) {
        EndRotation("rotate input released");
    }
}

// PollEventHook
void OnFrameStart()
{
    if (!g_installed) {
        return;
    }
    UpdateMouselook();
    std::lock_guard lock(g_rotation.mutex);
    if (g_rotation.should_override_cursor) {
        if (!g_rotation.overridden_last_frame) {
            g_sdl.SetRelativeMouseMode(1);
            g_rotation.virtual_cursor = VirtualCursor::RequestOverride;
        }
        // Hide the cursor only once the mouse has moved: a plain right-click stays a click.
        const int dx = g_rotation.restore.x - g_rotation.virtual_position.x;
        const int dy = g_rotation.restore.y - g_rotation.virtual_position.y;
        if (g_rotation.virtual_cursor == VirtualCursor::RequestOverride &&
            dx * dx + dy * dy > kOverrideDistanceSquared) {
            g_rotation.virtual_cursor = VirtualCursor::Override;
            PushMouseMotion();
        }
    } else if (g_rotation.overridden_last_frame) {
        void* window = Window();
        if (window) {
            g_sdl.WarpMouseInWindow(window, g_rotation.restore.x, g_rotation.restore.y);
        }
        LOG_INFO("Cursor restored at %d,%d%s", g_rotation.restore.x, g_rotation.restore.y,
            window ? "" : " (no window to warp in)");
        g_sdl.SetRelativeMouseMode(0);
        g_rotation.virtual_cursor = VirtualCursor::EndOverride;
        PushMouseMotion();
    }
    g_rotation.overridden_last_frame = g_rotation.should_override_cursor;
}

bool Install(const mem::MainExecutable& exe, uintptr_t execute_camera_command)
{
    auto file_address = [&](uintptr_t address) { return static_cast<unsigned long>(address - exe.load_bias); };
    if (!g_sdl.Load()) {
        LOG_WARN("Camera rotation features off: SDL functions not found");
        return false;
    }
    RotationAddresses rotation;
    std::string error;
    if (!FindRotationAddresses(reinterpret_cast<const uint8_t*>(exe.code.start), exe.code.size,
            exe.code.start, execute_camera_command, rotation, error)) {
        LOG_WARN("Camera rotation features off: %s", error.c_str());
        return false;
    }

    void* cursor_stub = mem::MakeJumpStub(exe.code.start, reinterpret_cast<const void*>(&SetVirtualCursorPosHook));
    if (!cursor_stub) {
        LOG_WARN("Camera rotation features off: could not allocate a jump stub");
        return false;
    }
    g_virtual_cursor_slot = rotation.set_virtual_cursor_slot;
    // Activate the frame logic first: once a hook is in, rotation can start at any time.
    g_installed = true;

    if (!mem::ReplaceVirtualCall(rotation.set_virtual_cursor_call, cursor_stub)) {
        LOG_WARN("Camera rotation features off: could not hook SetVirtualCursorPos");
        g_installed = false;
        return false;
    }

    // The cursor manager's push and pop are hooked at their entries, so cursor changes the UI makes
    // through function pointers are seen too. Neither prologue depends on its address.
    static constexpr uint8_t kPushEntry[] = { 0x55, 0x53, 0x50, 0x48, 0x89, 0xF8 };       // push rbp, rbx, rax; mov rax, rdi
    static constexpr uint8_t kPopEntry[] = { 0x48, 0x8B, 0x47, 0x30, 0x48, 0x85, 0xC0 };  // mov rax, [rdi+0x30]; test rax, rax
    if (!mem::HookEntry(rotation.cursor_push, kPushEntry, sizeof kPushEntry,
            reinterpret_cast<const void*>(&CursorPushHook), reinterpret_cast<void**>(&g_cursor_push))) {
        LOG_WARN("Rotation cursor unchanged: could not hook the cursor push");
        return false;
    }
    if (!mem::HookEntry(rotation.cursor_pop, kPopEntry, sizeof kPopEntry, reinterpret_cast<const void*>(&CursorPopHook),
            reinterpret_cast<void**>(&g_cursor_pop))) {
        // A rotation start without its stop would leave the cursor hidden.
        mem::UnhookEntry(rotation.cursor_push, kPushEntry, sizeof kPushEntry);
        LOG_WARN("Rotation cursor unchanged: could not hook the cursor pop");
        return false;
    }
    LOG_INFO("Rotation cursor push %#lx and pop %#lx hooked at entry, SetVirtualCursorPos call %#lx slot %#x",
        file_address(rotation.cursor_push), file_address(rotation.cursor_pop),
        file_address(rotation.set_virtual_cursor_call), rotation.set_virtual_cursor_slot);
    return true;
}

}  // namespace wasd::camera

// The game calls SDL_GetWindowGrab from its event loop; this records the window for warping the
// cursor back after rotation.
extern "C" __attribute__((visibility("default"))) int SDL_GetWindowGrab(void* window)
{
    using GetWindowGrabFn = int (*)(void*);
    static const auto real = reinterpret_cast<GetWindowGrabFn>(dlsym(RTLD_NEXT, "SDL_GetWindowGrab"));
    if (window) {
        wasd::camera::SetWindow(window);
    }
    return real ? real(window) : 0;
}
