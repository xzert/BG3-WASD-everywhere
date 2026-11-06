#include "camera.hpp"

#include <dlfcn.h>

#include <atomic>
#include <cstring>
#include <mutex>

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
               load(GetKeyboardFocus, "SDL_GetKeyboardFocus");
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

}  // namespace

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

    void* push_stub = mem::MakeJumpStub(exe.code.start, reinterpret_cast<const void*>(&CursorPushHook));
    void* pop_stub = mem::MakeJumpStub(exe.code.start, reinterpret_cast<const void*>(&CursorPopHook));
    void* cursor_stub = mem::MakeJumpStub(exe.code.start, reinterpret_cast<const void*>(&SetVirtualCursorPosHook));
    if (!push_stub || !pop_stub || !cursor_stub) {
        LOG_WARN("Camera rotation features off: could not allocate jump stubs");
        return false;
    }

    g_cursor_push = reinterpret_cast<CursorFn>(rotation.cursor_push);
    g_cursor_pop = reinterpret_cast<CursorFn>(rotation.cursor_pop);
    g_virtual_cursor_slot = rotation.set_virtual_cursor_slot;
    // Activate the frame logic first: once a hook is in, rotation can start at any time.
    g_installed = true;

    if (!mem::ReplaceVirtualCall(rotation.set_virtual_cursor_call, cursor_stub)) {
        LOG_WARN("Camera rotation features off: could not hook SetVirtualCursorPos");
        g_installed = false;
        return false;
    }
    for (const uintptr_t call : rotation.push_calls) {
        mem::RedirectCall(call, push_stub);
    }
    for (const uintptr_t call : rotation.pop_calls) {
        mem::RedirectCall(call, pop_stub);
    }
    LOG_INFO("Rotation cursor push %#lx (%zu calls), pop %#lx (%zu calls), SetVirtualCursorPos call %#lx slot %#x",
        file_address(rotation.cursor_push), rotation.push_calls.size(), file_address(rotation.cursor_pop),
        rotation.pop_calls.size(), file_address(rotation.set_virtual_cursor_call), rotation.set_virtual_cursor_slot);
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
