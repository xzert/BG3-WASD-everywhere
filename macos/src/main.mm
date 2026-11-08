// BG3 WASD Everywhere for the native macOS game on Apple Silicon, an early port.
//
// - The movement keys move the character. Like WASDUnlockPatch on Windows, it removes the check that
//   keeps the gamepad movement task (ecl::CharacterTask_MoveController) for gamepads.
// - It binds the movement keys in inputconfig_p1.json, like InputconfigPatcher on Windows.
// - Hotkeys and settings come from bg3-wasd-everywhere.toml next to the library, read with the Linux
//   port's code and reloaded about a second after the file changes.
//   - Movement mode: ToggleMovementMode, HoldMovementMode. In Character mode the movement keys don't
//     pan the camera. In Camera mode they only pan it: the movement input of
//     ecl::CharacterTask_MoveController (GetRotatedInput, inline-hooked) is zero.
//   - Walking: ToggleWalkspeed, HoldWalkspeed and WalkSpeed scale that movement input.
//   - Autoforward: ToggleAutoforward holds the Forward key down for the player.
//   - ReloadConfig.
//   - While a text field of the game has the keyboard focus, key presses don't trigger hotkeys.
// - Camera rotation: while the game shows its rotation cursor, the cursor is detached from the
//   mouse and hidden ("invisible") or centered ("centered"), and rotate + left click walks forward.
// - Mouselook (ToggleCameraRotation): the mod holds the rotate input for the player, and lets go
//   of it in menus, dialogs, panels, text fields and loading screens.
// - It logs the raw camera input events and key events, for the next steps of the port. Key events
//   aren't logged while the player types into a text field.

#import <AppKit/AppKit.h>

#include <ApplicationServices/ApplicationServices.h>
#include <dlfcn.h>
#include <libkern/OSCacheControl.h>
#include <libproc.h>
#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <mach/mach_error.h>
#include <mach/mach_vm.h>
#include <objc/runtime.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>

#include "arm64.hpp"
#include "hotkeys.hpp"
#include "inputconfig.hpp"
#include "keycodes.hpp"
#include "log.hpp"
#include "macho.hpp"
#include "patterns.hpp"
#include "settings.hpp"
#include "symbols.hpp"

#if !defined(__arm64__)
#error "The macOS port supports Apple Silicon (arm64) only."
#endif

// Defined in stubs.S: the pass-through stubs for ecl::CameraSystem::OnInputEvent and
// gui::GameUI::Update, where they continue, and the GameUI object the second one remembers.
extern "C" {
void CameraInputProbe();
extern void* g_camera_input_original;
void GameUIUpdateProbe();
extern void* g_game_ui_update_original;
extern void* g_game_ui;
}

namespace {

using namespace wasd;
using hotkeys::Command;
using hotkeys::Key;

constexpr const char* kGameName = "Baldur's Gate 3";
constexpr const char* kConfigName = "bg3-wasd-everywhere.toml";
constexpr int kMaxObservedLines = 3000;  // keeps a long session from growing the log without end

// The game's camera movement actions: CameraBackward, CameraForward, CameraLeft, CameraRight.
constexpr uint32_t kFirstCameraMove = 99, kLastCameraMove = 102;

// No objects with constructors at namespace scope: see "No static initializers" in CLAUDE.md.
std::atomic<int> g_observed_lines{ 0 };
IMP g_original_send_event = nullptr;
uintptr_t g_controller_mode_flag = 0;  // the game's byte, set while a gamepad is in use

// Movement mode and walking, as in the Linux port: Character mode is "toggled" unless held.
std::atomic<bool> g_mode_toggled{ true };
std::atomic<bool> g_mode_held{ false };
std::atomic<bool> g_walking_toggled{ false };
std::atomic<bool> g_walking_held{ false };
std::atomic<float> g_walk_speed{ 0.3f };

// The rotation cursor in each movement mode, and rotate + left click (CameraRotation settings).
enum class RotationCursor : uint8_t { Invisible, Centered, Vanilla };
std::atomic<RotationCursor> g_cursor_character{ RotationCursor::Invisible };
std::atomic<RotationCursor> g_cursor_camera{ RotationCursor::Invisible };
std::atomic<bool> g_rotate_plus_left_click{ true };
bool g_rotate_input_is_left_click = false;  // CameraToggleMouseRotate includes the left button
std::atomic<bool> g_mode_toggles_mouselook{ false };  // TogglingMovementModeAlsoTogglesCameraRotation
std::atomic<bool> g_prevent_clicking_character{ true };  // PreventClickingActiveCharacter

// Main thread only, like sendEvent:.
bool g_autoforward = false;
int g_forward_virtual_key = 13;  // the key autoforward holds down: the first Forward key, W by default
int g_swallowed_key = -1;        // a key the game doesn't get until its release: the autoforward hotkey
int g_last_caps_lock = -1;
struct timespec g_config_mtime = {};
dispatch_source_t g_config_timer = nullptr;

bool IsCharacterMode() { return g_mode_toggled.load() != g_mode_held.load(); }
bool IsWalking() { return g_walking_toggled.load() != g_walking_held.load(); }

bool TakeLine()
{
    return g_observed_lines.fetch_add(1, std::memory_order_relaxed) < kMaxObservedLines;
}

int64_t NowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// ~/Library/Logs/bg3-wasd-everywhere/bg3-wasd-everywhere.log
std::string LogPath()
{
    const char* home = std::getenv("HOME");
    std::string dir = std::string(home && *home ? home : "/tmp") + "/Library/Logs/bg3-wasd-everywhere";
    mkdir(dir.c_str(), 0755);
    return dir + "/bg3-wasd-everywhere.log";
}

// bg3-wasd-everywhere.toml next to the library. A function-local static, like every object with a
// constructor here.
std::string& ConfigPath()
{
    static std::string path;
    return path;
}

std::string LibraryDirectory()
{
    Dl_info info{};
    if (!dladdr(reinterpret_cast<void*>(&LibraryDirectory), &info) || !info.dli_fname) {
        return ".";
    }
    const std::string path = info.dli_fname;
    const size_t slash = path.rfind('/');
    return slash == std::string::npos ? "." : path.substr(0, slash);
}

// --- Settings ---------------------------------------------------------------------------------

// "invisible", "centered" or "vanilla"; anything else falls back to invisible, as on Linux.
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

const char* RotationCursorName(RotationCursor cursor)
{
    return cursor == RotationCursor::Centered ? "centered" : cursor == RotationCursor::Vanilla ? "vanilla" : "invisible";
}

// Whether the config file changed since the last call; remembers its current version.
bool ConfigChanged()
{
    struct stat info {};
    if (stat(ConfigPath().c_str(), &info) != 0) {
        return false;
    }
    const bool changed = info.st_mtimespec.tv_sec != g_config_mtime.tv_sec ||
        info.st_mtimespec.tv_nsec != g_config_mtime.tv_nsec;
    g_config_mtime = info.st_mtimespec;
    return changed;
}

// The first load also sets the initial movement mode and walking, as on Linux.
void LoadSettings(bool first)
{
    ConfigChanged();  // the version read below, so the watch doesn't load it again
    Settings settings;
    if (ParseSettingsFile(ConfigPath(), settings)) {
        LOG_INFO("Config loaded from %s", ConfigPath().c_str());
    } else {
        LOG_WARN("Could not read %s, using defaults", ConfigPath().c_str());
    }
    if (settings.walk_speed < 0.0f || settings.walk_speed > 1.0f) {
        LOG_WARN("WalkSpeed %.2f is outside [0, 1], clamping", settings.walk_speed);
        settings.walk_speed = settings.walk_speed < 0.0f ? 0.0f : 1.0f;
    }
    hotkeys::SetBindings(settings);
    g_walk_speed = settings.walk_speed;
    g_cursor_character = ParseRotationCursor(settings.rotation_cursor_in_character_mode);
    g_cursor_camera = ParseRotationCursor(settings.rotation_cursor_in_camera_mode);
    g_rotate_plus_left_click = settings.rotate_plus_lmb_is_forward;
    g_mode_toggles_mouselook = settings.toggling_movement_mode_also_toggles_camera_rotation;
    g_prevent_clicking_character = settings.prevent_clicking_active_character;
    if (first) {
        g_mode_toggled = settings.is_character_movement_default;
        g_walking_toggled = settings.is_walking_default;
    }
}

// Reloads the config about a second after it changes, on the main thread like the hotkeys. A timer
// rather than a file watch: editors save in different ways, and this sees all of them. The timer
// stays referenced for the life of the process: a released timer stops.
void WatchSettings()
{
    g_config_timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
    if (!g_config_timer) {
        LOG_WARN("Can't watch %s; ReloadConfig still reloads it", ConfigPath().c_str());
        return;
    }
    dispatch_source_set_timer(g_config_timer, dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), NSEC_PER_SEC,
        NSEC_PER_SEC / 4);
    dispatch_source_set_event_handler(g_config_timer, ^{
        if (ConfigChanged()) {
            LoadSettings(false);
        }
    });
    dispatch_resume(g_config_timer);
}

// --- Patching ---------------------------------------------------------------------------------

const macho::Header64* MainImage(intptr_t& slide)
{
    for (uint32_t i = 0, count = _dyld_image_count(); i < count; ++i) {
        auto header = reinterpret_cast<const macho::Header64*>(_dyld_get_image_header(i));
        if (header && header->magic == macho::kMagic64 && header->filetype == macho::kExecutable) {
            slide = _dyld_get_image_vmaddr_slide(i);
            return header;
        }
    }
    return nullptr;
}

// Vtables live in __DATA_CONST, which dyld makes read-only after binding.
bool WritePointer(uintptr_t slot, uintptr_t value)
{
    const uintptr_t page_size = uintptr_t(getpagesize());
    void* page = reinterpret_cast<void*>(slot & ~(page_size - 1));
    if (mprotect(page, page_size, PROT_READ | PROT_WRITE) != 0) {
        LOG_ERROR("mprotect(read/write) of a vtable failed: %s", std::strerror(errno));
        return false;
    }
    *reinterpret_cast<volatile uintptr_t*>(slot) = value;
    if (mprotect(page, page_size, PROT_READ) != 0) {
        LOG_WARN("mprotect(read) of a vtable failed: %s", std::strerror(errno));
    }
    return true;
}

// Overwrites game code. Its pages are mapped read/execute and can't be made writable, so
// VM_PROT_COPY first gives the process its own copy of them.
bool WriteCode(uintptr_t address, const void* bytes, size_t size)
{
    const mach_vm_address_t page = address & ~mach_vm_address_t(vm_page_size - 1);
    const mach_vm_size_t length = (address + size - page + vm_page_size - 1) & ~mach_vm_size_t(vm_page_size - 1);
    kern_return_t result = mach_vm_protect(mach_task_self(), page, length, FALSE,
        VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);
    if (result != KERN_SUCCESS) {
        LOG_ERROR("mach_vm_protect(read/write/copy) at %#lx failed: %d (%s)", (unsigned long)address, result,
            mach_error_string(result));
        return false;
    }
    std::memcpy(reinterpret_cast<void*>(address), bytes, size);
    result = mach_vm_protect(mach_task_self(), page, length, FALSE, VM_PROT_READ | VM_PROT_EXECUTE);
    if (result != KERN_SUCCESS) {
        LOG_ERROR("mach_vm_protect(read/execute) at %#lx failed: %d (%s); the game will crash in this page",
            (unsigned long)address, result, mach_error_string(result));
        return false;
    }
    sys_icache_invalidate(reinterpret_cast<void*>(address), size);
    return true;
}

// Copies the first 4 instructions of `target` into a trampoline that jumps on to target + 16, then
// overwrites them with a jump to `hook`. `*original` points at the trampoline before the patch, so
// calling it always reaches the original function.
bool InlineHook(const char* name, uintptr_t target, uintptr_t hook, void** original)
{
    uint32_t prologue[4];
    std::memcpy(prologue, reinterpret_cast<const void*>(target), sizeof prologue);
    for (uint32_t instruction : prologue) {
        if (arm64::IsPcRelative(instruction)) {
            LOG_ERROR("Inline hook: %s starts with a PC-relative instruction %08x", name, instruction);
            return false;
        }
    }

    uint32_t trampoline[8];
    std::memcpy(trampoline, prologue, sizeof prologue);
    arm64::EncodeAbsoluteJump(target + sizeof prologue, trampoline + 4);
    void* memory = mmap(nullptr, vm_page_size, PROT_READ | PROT_WRITE | PROT_EXEC,
        MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
    if (memory == MAP_FAILED) {
        LOG_ERROR("Inline hook: mmap(MAP_JIT) for %s failed: %s", name, std::strerror(errno));
        return false;
    }
    pthread_jit_write_protect_np(0);
    std::memcpy(memory, trampoline, sizeof trampoline);
    pthread_jit_write_protect_np(1);
    sys_icache_invalidate(memory, sizeof trampoline);
    *original = memory;

    uint32_t patch[4];
    arm64::EncodeAbsoluteJump(hook, patch);
    if (!WriteCode(target, patch, sizeof patch)) {
        return false;
    }
    LOG_INFO("Inline hook: %s at %#lx, trampoline %p", name, (unsigned long)target, memory);
    return true;
}

// The code of the function at `start`, up to the next symbol; empty if that looks wrong.
const uint32_t* FunctionCode(const macho::Image& image, uintptr_t start, size_t& count)
{
    const uintptr_t end = start ? macho::NextSymbolAddress(image, start) : 0;
    count = end > start && end - start <= 0x10000 ? (end - start) / 4 : 0;
    return count ? reinterpret_cast<const uint32_t*>(start) : nullptr;
}

// The movement keys drive ecl::CharacterTask_MoveController, the task the game otherwise runs only
// for a gamepad: its CanExecute() gives up unless the controller-mode flag is set. That check
// becomes a nop, like WASDUnlockPatch on Windows and on Linux.
bool InstallWasdUnlock(const macho::Image& image, const uintptr_t* addresses)
{
    size_t setter_count = 0, check_count = 0;
    const uintptr_t setter = addresses[symbols::SetKeyboardMode];
    const uintptr_t can_execute = addresses[symbols::MoveControllerCanExecute];
    const uint32_t* setter_code = FunctionCode(image, setter, setter_count);
    const uint32_t* check_code = FunctionCode(image, can_execute, check_count);
    if (!setter_code || !check_code) {
        LOG_ERROR("WASD unlock: SetKeyboardMode or CharacterTask_MoveController::CanExecute not found");
        return false;
    }
    const uint64_t flag = patterns::FindControllerModeFlag(setter_code, setter_count, setter);
    const long check = flag ? patterns::FindControllerModeCheck(check_code, check_count, can_execute, flag) : -1;
    if (check < 0) {
        LOG_ERROR("WASD unlock: controller-mode check not found (flag %#llx)", (unsigned long long)flag);
        return false;
    }
    const uintptr_t site = can_execute + 4 * uintptr_t(check);
    LOG_INFO("WASD unlock: controller-mode flag %#llx (now %d), check at %#lx (%08x)", (unsigned long long)flag,
        int(*reinterpret_cast<const volatile uint8_t*>(flag)), (unsigned long)site, check_code[check]);
    const uint32_t nop = arm64::kNop;
    if (!WriteCode(site, &nop, sizeof nop)) {
        return false;
    }
    g_controller_mode_flag = uintptr_t(flag);
    LOG_INFO("WASD movement enabled");
    return true;
}

// In Character mode the camera doesn't get the movement keys' camera actions: the stub in stubs.S
// passes every ecl::CameraSystem::OnInputEvent call through ObserveCameraInput first.
void InstallCameraInputHook(const uintptr_t* addresses)
{
    const uintptr_t vtable = addresses[symbols::CameraSystemVtable];
    const uintptr_t function = addresses[symbols::CameraSystemOnInputEvent];
    if (!vtable || !function) {
        LOG_ERROR("ecl::CameraSystem not found: the movement keys will pan the camera");
        return;
    }
    const uintptr_t slot = vtable + symbols::kVtableAddressPoint + symbols::kCameraOnInputEventSlot;
    const uintptr_t current = *reinterpret_cast<const uintptr_t*>(slot);
    if (current != function) {
        LOG_ERROR("CameraSystem vtable slot %#lx holds %#lx, not OnInputEvent %#lx", (unsigned long)slot,
            (unsigned long)current, (unsigned long)function);
        return;
    }
    g_camera_input_original = reinterpret_cast<void*>(current);
    if (WritePointer(slot, reinterpret_cast<uintptr_t>(&CameraInputProbe))) {
        LOG_INFO("Camera input hooked: ecl::CameraSystem::OnInputEvent (vtable slot %#lx)", (unsigned long)slot);
    }
}

// Whether a text field has the keyboard focus is the game's own answer:
// gui::GameUI::IsTextInputElementFocused. It needs the game's GameUI object, and the engine calls
// GameUI::Update with it every frame through the vtable, so the stub in that slot remembers it.
using IsTextInputFocusedFn = bool (*)(const void* game_ui);
IsTextInputFocusedFn g_is_text_input_focused = nullptr;

void InstallTextInputCheck(const uintptr_t* addresses)
{
    const uintptr_t vtable = addresses[symbols::GameUIVtable];
    const uintptr_t update = addresses[symbols::GameUIUpdate];
    const uintptr_t focused = addresses[symbols::IsTextInputElementFocused];
    if (!vtable || !update || !focused) {
        LOG_WARN("gui::GameUI not found: hotkeys also work while you type in a text field");
        return;
    }
    const uintptr_t first = vtable + symbols::kVtableAddressPoint;
    for (size_t i = 0; i < symbols::kGameUIVtableSlots; ++i) {
        const uintptr_t slot = first + 8 * i;
        if (*reinterpret_cast<const uintptr_t*>(slot) != update) {
            continue;
        }
        g_game_ui_update_original = reinterpret_cast<void*>(update);
        if (WritePointer(slot, reinterpret_cast<uintptr_t>(&GameUIUpdateProbe))) {
            g_is_text_input_focused = reinterpret_cast<IsTextInputFocusedFn>(focused);
            LOG_INFO("Text field check: gui::GameUI::Update hooked (vtable slot %#zx)", 8 * i);
        }
        return;
    }
    LOG_WARN("gui::GameUI::Update isn't in its vtable: hotkeys also work while you type in a text field");
}

// True while a text field of the game has the keyboard focus: a save name, the chat. Main thread.
bool TextInputFocused()
{
    void* game_ui = __atomic_load_n(&g_game_ui, __ATOMIC_ACQUIRE);
    const bool focused = game_ui && g_is_text_input_focused && g_is_text_input_focused(game_ui);
    static bool logged = false;
    if (focused != logged) {
        logged = focused;
        LOG_INFO("%s", focused ? "Text field focused: key presses don't trigger hotkeys" : "Text field closed");
    }
    return focused;
}

// (anonymous namespace)::GetRotatedInput(short, bool) returns the movement input turned to the
// camera, a Vector3f in s0-s2. The movement task asks for it before it starts and on every update.
struct Vector3f {
    float x, y, z;
};
using GetRotatedInputFn = Vector3f (*)(int16_t player, bool flag);
GetRotatedInputFn g_get_rotated_input = nullptr;  // the trampoline to the original

// Camera mode: no movement, so the task doesn't start and stops if it runs. Walking: a shorter
// vector, which the game moves at walking speed, like a gamepad stick pushed part of the way. Like
// the Linux port's OverrideMovementInput. A gamepad keeps working as without the mod.
std::atomic<int64_t> g_last_movement_poll_ms{ 0 };  // when the game last asked for the movement input

Vector3f RotatedInputHook(int16_t player, bool flag)
{
    g_last_movement_poll_ms.store(NowMs(), std::memory_order_relaxed);
    Vector3f input = g_get_rotated_input(player, flag);
    if (g_controller_mode_flag && *reinterpret_cast<const volatile uint8_t*>(g_controller_mode_flag)) {
        return input;  // a gamepad
    }
    if (!IsCharacterMode()) {
        return { 0.0f, 0.0f, 0.0f };
    }
    if (IsWalking()) {
        const float speed = g_walk_speed.load(std::memory_order_relaxed);
        input = { input.x * speed, input.y * speed, input.z * speed };
    }
    return input;
}

void InstallMovementHook(const uintptr_t* addresses)
{
    const uintptr_t target = addresses[symbols::GetRotatedInput];
    if (!target) {
        LOG_ERROR("GetRotatedInput not found: no Camera mode and no walking");
        return;
    }
    InlineHook("GetRotatedInput", target, reinterpret_cast<uintptr_t>(&RotatedInputHook),
        reinterpret_cast<void**>(&g_get_rotated_input));
}

// Mouselook holds the first plain binding of CameraToggleMouseRotate (no modifiers), as on Linux.
struct PressedInput {
    bool mouse_button;
    int code;
};
PressedInput g_rotate_input = { true, 2 };  // the middle mouse button, the game's default

void ChooseRotateInput(const std::vector<std::string>& bindings)
{
    std::string name = "mouse:middle";
    for (const std::string& binding : bindings) {
        if (binding.find('+') == std::string::npos && binding != "mouse:left") {
            name = binding;
            break;
        }
    }
    bool mouse_button = false;
    int code = 0;
    if (hotkeys::LookupKey(name, mouse_button, code) &&
        (mouse_button || keycodes::VirtualKeyFromHid(code) >= 0)) {
        g_rotate_input = { mouse_button, code };
        LOG_INFO("Mouselook holds %s", name.c_str());
    } else {
        LOG_WARN("Mouselook can't hold \"%s\"; using the middle mouse button", name.c_str());
    }
}

// The game keeps player profiles in ~/Documents on macOS; older builds used Application Support.
// The game's own Forward and Backward keys then also end autoforward, as on Linux.
void BindMovementKeys()
{
    const char* home = std::getenv("HOME");
    if (!home || !*home) {
        return;
    }
    for (const char* base : { "/Documents/Larian Studios/Baldur's Gate 3/PlayerProfiles",
             "/Library/Application Support/Larian Studios/Baldur's Gate 3/PlayerProfiles" }) {
        const std::string profiles = std::string(home) + base;
        struct stat info {};
        if (stat(profiles.c_str(), &info) == 0 && S_ISDIR(info.st_mode)) {
            const inputconfig::Result bindings = inputconfig::PatchProfiles(profiles);
            if (bindings.ok) {
                hotkeys::SetMovementKeys(bindings.forward_keys, bindings.backward_keys);
                g_rotate_input_is_left_click = bindings.rotate_includes_left_click;
                ChooseRotateInput(bindings.rotate_keys);
            }
            return;
        }
    }
    LOG_WARN("No player profiles found yet; start the game once, then again to bind the movement keys");
}

// Autoforward holds the first key of the game's Forward binding.
void ChooseForwardKey()
{
    for (const hotkeys::Combo& combo : hotkeys::Bindings(hotkeys::Forward)) {
        if (combo.size() == 1 && combo[0].kind == Key::Scancode) {
            const int virtual_key = keycodes::VirtualKeyFromHid(combo[0].code);
            if (virtual_key >= 0) {
                g_forward_virtual_key = virtual_key;
                return;
            }
        }
    }
}

// --- Hotkeys ----------------------------------------------------------------------------------

void CallOriginalSendEvent(id application, NSEvent* event)
{
    reinterpret_cast<void (*)(id, SEL, NSEvent*)>(g_original_send_event)(application, @selector(sendEvent:), event);
}

// Gives the game a Forward key event of the mod's own, for the window `source` went to (or the key
// window, without an event).
void SendForwardKey(id application, NSEvent* source, bool down)
{
    NSString* characters = g_forward_virtual_key == 13 ? @"w" : @"";
    const NSInteger window = source ? source.windowNumber : NSApp.keyWindow.windowNumber;
    NSEvent* key = [NSEvent keyEventWithType:down ? NSEventTypeKeyDown : NSEventTypeKeyUp
                                    location:NSZeroPoint
                               modifierFlags:0
                                   timestamp:NSProcessInfo.processInfo.systemUptime
                                windowNumber:window
                                     context:nil
                                  characters:characters
                 charactersIgnoringModifiers:characters
                                   isARepeat:NO
                                     keyCode:static_cast<unsigned short>(g_forward_virtual_key)];
    if (key) {
        CallOriginalSendEvent(application, key);
    }
}

void SetAutoforward(id application, NSEvent* source, bool on)
{
    if (on == g_autoforward) {
        return;
    }
    g_autoforward = on;
    SendForwardKey(application, source, on);
    LOG_INFO("Autoforward %s", on ? "on" : "off");
}

// --- Camera rotation --------------------------------------------------------------------------
//
// While the rotate input is held, the game turns the camera by the mouse's deltaX and deltaY and
// shows its rotation cursor through ecl::CursorControl (ecl::EMouseCursorMode 0x2E, as on Linux).
// The cursor itself keeps moving, as in vanilla. Like CameraRotationOnHook on Windows, the mod takes
// the cursor over meanwhile, on the main thread:
// - The cursor is detached from the mouse (CGAssociateMouseAndMouseCursorPosition), so it can't hit
//   the screen edge or leave the window, and the game still gets the deltas.
// - "invisible": the cursor is hidden and stays where it was. The game's cursor position, the first
//   two floats of gCore that -[LSMTLView mouseMoved:] writes, is moved off-screen, so nothing gets
//   hovered or clicked.
// - "centered": the cursor moves to the window's center and comes back afterwards.
// It takes over only once the mouse has moved, so a plain click of the rotate input stays a click.

constexpr int kSyntheticTrigger = -2;  // the trigger of a rotation that mouselook started
bool g_rotate_held = false;            // mouselook holds the rotate input down (main thread)
float g_cursor_before_press[2] = {};   // the game's cursor position, moved off-screen for that press
bool g_have_cursor_before_press = false;
void UpdateMouselook();

constexpr uint32_t kRotationCursorMode = 0x2E;
constexpr double kOverrideDistanceSquared = 6.0 * 6.0;
constexpr float kOffScreen = -1000000.0f;

// Main thread only.
struct Rotation {
    bool active;          // between the game's start and end of rotation
    bool overridden;      // the mouse moved far enough: the cursor is taken over
    bool detached;        // the cursor is detached from the mouse
    bool hidden;          // the cursor is hidden
    bool warped;          // the cursor was moved to the center
    RotationCursor type;
    double moved_x, moved_y;
    CGPoint cursor;           // where the cursor was, in global display coordinates
    float game_cursor[2];     // the game's cursor position then
    bool have_game_cursor;
    PressedInput trigger;     // the press that started rotation
};
Rotation g_rotation = {};
PressedInput g_last_press = { false, -1 };
bool g_rotate_forward = false;          // rotate + left click holds Forward
bool g_left_click_swallowed = false;    // the game didn't get the left button's press
float** g_core = nullptr;               // gCore

bool IsControllerMode()
{
    return g_controller_mode_flag && *reinterpret_cast<const volatile uint8_t*>(g_controller_mode_flag);
}

// The cursor is taken only while the game is the active app, and whatever was taken is given back
// even after the game stopped being active. The stand-in test has no NSApplication, so it leaves
// the cursor alone.
bool GameInFront() { return NSApp.isActive; }

float* GameCursor() { return g_core ? *g_core : nullptr; }

void SetCursorDetached(bool detached)
{
    if (detached == g_rotation.detached || (detached && !GameInFront())) {
        return;
    }
    CGAssociateMouseAndMouseCursorPosition(!detached);
    g_rotation.detached = detached;
}

// A warp stops the mouse for about a quarter second, unless the association is set right after it
// (SDL does the same).
void WarpCursor(CGPoint point)
{
    CGWarpMouseCursorPosition(point);
    CGAssociateMouseAndMouseCursorPosition(true);
    if (g_rotation.detached) {
        CGAssociateMouseAndMouseCursorPosition(false);
    }
}

// The center of the game window, in global display coordinates (origin at the top left).
bool WindowCenter(CGPoint& center)
{
    NSWindow* window = NSApp.keyWindow ?: NSApp.mainWindow;
    NSScreen* primary = NSScreen.screens.firstObject;
    if (!window || !primary) {
        return false;
    }
    const NSRect frame = window.frame;
    center = CGPointMake(NSMidX(frame), NSMaxY(primary.frame) - NSMidY(frame));
    return true;
}

void EndRotateForward(id application, NSEvent* source)
{
    if (!g_rotate_forward) {
        return;
    }
    g_rotate_forward = false;
    // As on Linux, letting go also ends autoforward.
    if (g_autoforward) {
        g_autoforward = false;
        LOG_INFO("Autoforward off");
    }
    SendForwardKey(application, source, false);
}

void OnRotationStart()
{
    if (g_rotation.active) {
        return;
    }
    g_rotation = {};
    g_rotation.active = true;
    g_rotation.type = IsCharacterMode() ? g_cursor_character.load() : g_cursor_camera.load();
    // Mouselook's own press starts it: no release of the player's ends it.
    g_rotation.trigger = g_rotate_held ? PressedInput{ true, kSyntheticTrigger } : g_last_press;
    if (g_rotation.trigger.code == kSyntheticTrigger && g_have_cursor_before_press) {
        g_rotation.game_cursor[0] = g_cursor_before_press[0];
        g_rotation.game_cursor[1] = g_cursor_before_press[1];
        g_rotation.have_game_cursor = true;
    } else if (const float* cursor = GameCursor()) {
        g_rotation.game_cursor[0] = cursor[0];
        g_rotation.game_cursor[1] = cursor[1];
        g_rotation.have_game_cursor = true;
    }
    if (CGEventRef now = CGEventCreate(nullptr)) {
        g_rotation.cursor = CGEventGetLocation(now);
        CFRelease(now);
    }
    if (g_rotation.type != RotationCursor::Vanilla) {
        SetCursorDetached(true);
    }
    if (g_rotation.trigger.code == kSyntheticTrigger) {
        LOG_INFO("Rotation start (mouselook), cursor %s", RotationCursorName(g_rotation.type));
    } else {
        LOG_INFO("Rotation start (%s %d), cursor %s", g_rotation.trigger.mouse_button ? "mouse" : "key",
            g_rotation.trigger.code, RotationCursorName(g_rotation.type));
    }
}

void EndRotation(const char* reason)
{
    if (!g_rotation.active) {
        return;
    }
    EndRotateForward(NSApp, nil);
    SetCursorDetached(false);
    if (g_rotation.hidden) {
        [NSCursor unhide];
    }
    if (g_rotation.warped) {
        WarpCursor(g_rotation.cursor);
    }
    // Mouselook's press moved the game's cursor off-screen even if the mouse never moved.
    float* cursor = GameCursor();
    const bool moved_away = g_rotation.overridden || g_rotation.trigger.code == kSyntheticTrigger;
    if (moved_away && cursor && g_rotation.have_game_cursor) {
        cursor[0] = g_rotation.game_cursor[0];
        cursor[1] = g_rotation.game_cursor[1];
    }
    g_rotation.active = false;
    LOG_INFO("Rotation end: %s", reason);
}

bool IsMouseMove(NSEventType type)
{
    return type == NSEventTypeMouseMoved || type == NSEventTypeLeftMouseDragged ||
        type == NSEventTypeRightMouseDragged || type == NSEventTypeOtherMouseDragged;
}

// The mouse moved while the camera rotates: take the cursor over once it moved far enough.
void OnRotationMouseMoved(NSEvent* event)
{
    if (!g_rotation.active || g_rotation.overridden || g_rotation.type == RotationCursor::Vanilla) {
        return;
    }
    g_rotation.moved_x += event.deltaX;
    g_rotation.moved_y += event.deltaY;
    if (g_rotation.moved_x * g_rotation.moved_x + g_rotation.moved_y * g_rotation.moved_y <=
        kOverrideDistanceSquared) {
        return;
    }
    g_rotation.overridden = true;
    const char* front = GameInFront() ? "" : " (the game isn't the active app: the cursor stays as it is)";
    if (g_rotation.type == RotationCursor::Invisible) {
        if (GameInFront()) {
            [NSCursor hide];
            g_rotation.hidden = true;
        }
        LOG_INFO("Rotation cursor hidden at %.0f,%.0f%s", g_rotation.cursor.x, g_rotation.cursor.y, front);
    } else {
        CGPoint center = g_rotation.cursor;
        if (GameInFront() && WindowCenter(center)) {
            WarpCursor(center);
            g_rotation.warped = true;
        }
        LOG_INFO("Rotation cursor centered at %.0f,%.0f%s", center.x, center.y, front);
    }
}

// After the game got a mouse event: its cursor position stays off-screen while the cursor is
// invisible.
void AfterEvent(NSEvent* event)
{
    if (g_rotation.active && g_rotation.overridden && g_rotation.type == RotationCursor::Invisible &&
        IsMouseMove(event.type)) {
        if (float* cursor = GameCursor()) {
            cursor[1] = kOffScreen;
        }
    }
}

// Holding the rotate input and the left mouse button walks forward, as on Windows and Linux. The
// game doesn't get that click, which would send the character to the spot under the cursor. Returns
// true if the game shouldn't get the event.
bool OnLeftButton(id application, NSEvent* event, bool down)
{
    if (down) {
        if (!g_rotation.active || !g_rotate_plus_left_click || g_rotate_input_is_left_click || !IsCharacterMode()) {
            return false;
        }
        g_left_click_swallowed = true;
        if (!g_rotate_forward) {
            g_rotate_forward = true;
            if (!g_autoforward) {
                SendForwardKey(application, event, true);
            }
            LOG_INFO("Rotate + left click: forward");
        }
        return true;
    }
    if (!g_left_click_swallowed) {
        return false;
    }
    g_left_click_swallowed = false;
    EndRotateForward(application, event);
    return true;
}

// A key or mouse button went down or up. Rotation ends when its input is released, even if the game
// doesn't end it: when a menu opens mid-rotation and takes the release.
void TrackRotateInput(bool mouse_button, int code, bool down)
{
    if (down) {
        g_last_press = { mouse_button, code };
    } else if (g_rotation.active && g_rotation.trigger.mouse_button == mouse_button && g_rotation.trigger.code == code) {
        EndRotation("rotate input released");
    }
}

// Called by the game, on its own thread, when a forced cursor comes or goes.
using CursorModeFn = uint64_t (*)(void* control, uint32_t mode);
CursorModeFn g_add_forced_cursor = nullptr;
CursorModeFn g_remove_forced_cursor = nullptr;

uint64_t AddForcedCursorHook(void* control, uint32_t mode)
{
    if (mode == kRotationCursorMode && !IsControllerMode()) {
        dispatch_async(dispatch_get_main_queue(), ^{
            OnRotationStart();
        });
    }
    return g_add_forced_cursor(control, mode);
}

uint64_t RemoveForcedCursorHook(void* control, uint32_t mode)
{
    if (mode == kRotationCursorMode) {
        dispatch_async(dispatch_get_main_queue(), ^{
            EndRotation("the game ended it");
        });
    }
    return g_remove_forced_cursor(control, mode);
}

void InstallRotationHooks(const uintptr_t* addresses)
{
    const uintptr_t add = addresses[symbols::AddForcedMouseCursor];
    const uintptr_t remove = addresses[symbols::RemoveForcedMouseCursor];
    if (!add || !remove) {
        LOG_WARN("ecl::CursorControl not found: the rotation cursor stays as in the game");
        return;
    }
    g_core = reinterpret_cast<float**>(addresses[symbols::Core]);
    if (!g_core) {
        LOG_WARN("gCore not found: the game's cursor position isn't moved off-screen while rotating");
    }
    // The end first: a start without its end would leave the cursor hidden.
    if (!InlineHook("RemoveForcedMouseCursor", remove, reinterpret_cast<uintptr_t>(&RemoveForcedCursorHook),
            reinterpret_cast<void**>(&g_remove_forced_cursor)) ||
        !InlineHook("AddForcedMouseCursor", add, reinterpret_cast<uintptr_t>(&AddForcedCursorHook),
            reinterpret_cast<void**>(&g_add_forced_cursor))) {
        LOG_WARN("The rotation cursor stays as in the game");
        return;
    }
    // A cursor hidden or detached must not outlast the game's focus.
    [[NSNotificationCenter defaultCenter] addObserverForName:NSApplicationDidResignActiveNotification
                                                      object:nil
                                                       queue:NSOperationQueue.mainQueue
                                                  usingBlock:^(NSNotification*) {
                                                      EndRotation("the game went to the background");
                                                      UpdateMouselook();
                                                  }];
    LOG_INFO("Rotation cursor: %s in Character mode, %s in Camera mode", RotationCursorName(g_cursor_character),
        RotationCursorName(g_cursor_camera));
}

// --- Mouselook -------------------------------------------------------------------------------
//
// ToggleCameraRotation: the camera keeps rotating without holding the rotate input. As on Linux,
// the mod holds the game's rotate input (CameraToggleMouseRotate's binding, the middle mouse button
// by default) for the player: the game gets a press of the mod's own through the original
// sendEvent:, and rotation and its cursor then work as if the player held the button. The game
// treats every mouse button but the left and right one as the middle one. Mouselook lets go of the
// input, and takes it again afterwards, while:
// - the game is in the background or a text field has the focus;
// - the game menu is open (gui::GameUI::OnIsGameMenuOpenChanged, hooked in the GameUI vtable);
// - a dialog runs (ecl::GameAnalyticsSystem::RecordDialogStart and RecordDialogEnd);
// - a panel opened with its key is open (MouselookPauseKeys, as on Linux);
// - the game doesn't ask for the movement input, as in menus, the pause menu and loading screens.

constexpr int64_t kNoMovementPollMs = 250;
constexpr int kTicksToStartRotating = 3;  // 50 ms ticks
constexpr int kTicksBeforePress = 2;      // after a pause: the game may ignore a press right after its menu
constexpr int kMaxPressAttempts = 6;      // about a second of presses that don't start rotation

std::atomic<bool> g_mouselook{ false };
std::atomic<bool> g_game_menu_open{ false };
std::atomic<bool> g_dialog_active{ false };
// Main thread only.
bool g_real_rotate_swallowed = false;  // the player's own press of the rotate input, while the mod holds it
int g_swallowed_button = -1;           // a mouse button hotkey whose release the game doesn't get either
int g_ticks_since_press = 0;
int g_press_attempts = 0;
int g_ticks_to_wait = 0;
dispatch_source_t g_mouselook_timer = nullptr;

// Panels opened with their keys while mouselook was on. A function-local static, like every object
// with a constructor here.
std::vector<int>& OpenPanels()
{
    static std::vector<int> panels;
    return panels;
}

// The same button for the game: every mouse button but the left and right one is its middle one.
bool IsRotateInput(bool mouse_button, int code)
{
    if (mouse_button != g_rotate_input.mouse_button) {
        return false;
    }
    if (!mouse_button || code == g_rotate_input.code) {
        return code == g_rotate_input.code;
    }
    auto other = [](int button) { return button != 1 && button != 3; };
    return other(code) && other(g_rotate_input.code);
}

// Gives the game a press or release of the rotate input, for the key window.
void SendRotateInput(bool down)
{
    NSWindow* window = NSApp.keyWindow ?: NSApp.mainWindow;
    const NSTimeInterval now = NSProcessInfo.processInfo.systemUptime;
    NSEvent* event = nil;
    if (g_rotate_input.mouse_button) {
        const int button = g_rotate_input.code;
        const NSEventType type = button == 1 ? (down ? NSEventTypeLeftMouseDown : NSEventTypeLeftMouseUp)
            : button == 3                    ? (down ? NSEventTypeRightMouseDown : NSEventTypeRightMouseUp)
                                             : (down ? NSEventTypeOtherMouseDown : NSEventTypeOtherMouseUp);
        const NSPoint location = window ? window.mouseLocationOutsideOfEventStream : NSZeroPoint;
        event = [NSEvent mouseEventWithType:type
                                   location:location
                              modifierFlags:0
                                  timestamp:now
                               windowNumber:window.windowNumber
                                    context:nil
                                eventNumber:0
                                 clickCount:1
                                   pressure:down ? 1.0f : 0.0f];
    } else {
        const int virtual_key = keycodes::VirtualKeyFromHid(g_rotate_input.code);
        event = [NSEvent keyEventWithType:down ? NSEventTypeKeyDown : NSEventTypeKeyUp
                                 location:NSZeroPoint
                            modifierFlags:0
                                timestamp:now
                             windowNumber:window.windowNumber
                                  context:nil
                               characters:@""
              charactersIgnoringModifiers:@""
                                isARepeat:NO
                                  keyCode:static_cast<unsigned short>(virtual_key)];
    }
    if (event) {
        CallOriginalSendEvent(NSApp, event);
    }
}

// The game gives a press to the UI under its cursor, so a HUD button there would take the press
// meant for the camera. During the press the game's cursor is off-screen; rotation keeps it there
// or gives it back.
void PressRotateInput()
{
    if (float* cursor = GameCursor()) {
        g_cursor_before_press[0] = cursor[0];
        g_cursor_before_press[1] = cursor[1];
        g_have_cursor_before_press = true;
        cursor[1] = kOffScreen;
    }
    SendRotateInput(true);
    g_rotate_held = true;
    g_ticks_since_press = 0;
}

void ReleaseRotateInput(const char* reason)
{
    if (!g_rotate_held) {
        return;
    }
    SendRotateInput(false);
    g_rotate_held = false;
    // The game ends rotation for the release too; this gives the cursor back right away.
    if (g_rotation.active && g_rotation.trigger.code == kSyntheticTrigger) {
        EndRotation(reason);
    } else if (g_have_cursor_before_press) {
        float* cursor = GameCursor();
        if (cursor && cursor[1] == kOffScreen) {
            cursor[0] = g_cursor_before_press[0];
            cursor[1] = g_cursor_before_press[1];
        }
    }
    g_have_cursor_before_press = false;
}

// The stand-in test has no NSApplication: it counts as in front.
bool GameInBackground() { return NSApp && !NSApp.isActive; }

const char* MouselookPauseReason()
{
    if (GameInBackground()) {
        return "the game is in the background";
    }
    if (TextInputFocused()) {
        return "text input";
    }
    if (g_game_menu_open) {
        return "the game menu is open";
    }
    if (g_dialog_active) {
        return "dialog";
    }
    if (!OpenPanels().empty()) {
        return "a panel is open";
    }
    if (NowMs() - g_last_movement_poll_ms.load(std::memory_order_relaxed) > kNoMovementPollMs) {
        return "the game isn't taking movement input (menu, pause, loading)";
    }
    return nullptr;
}

// Every 50 ms, and right after mouselook or a pause reason changes. Main thread.
void UpdateMouselook()
{
    if (!g_mouselook) {
        OpenPanels().clear();
    }
    static const char* last_reason = nullptr;
    const char* reason = g_mouselook ? MouselookPauseReason() : nullptr;
    if (reason != last_reason) {
        if (reason) {
            LOG_INFO("Mouselook paused: %s", reason);
        } else if (g_mouselook) {
            LOG_INFO("Mouselook resumed");
            g_ticks_to_wait = kTicksBeforePress;
        }
        last_reason = reason;
    }
    if (!g_mouselook || reason) {
        ReleaseRotateInput(g_mouselook ? "mouselook paused" : "mouselook off");
        g_press_attempts = 0;
        return;
    }
    if (g_rotation.active) {
        g_press_attempts = 0;
        return;
    }
    // Not rotating: the press hasn't reached the camera yet, or the UI took it. Release, then press
    // again, a few times at most.
    if (g_rotate_held) {
        if (++g_ticks_since_press >= kTicksToStartRotating) {
            ReleaseRotateInput("rotation didn't start");
        }
    } else if (g_ticks_to_wait > 0) {
        --g_ticks_to_wait;
    } else if (g_press_attempts < kMaxPressAttempts) {
        PressRotateInput();
        ++g_press_attempts;
    } else {
        g_mouselook = false;
        LOG_WARN("Mouselook off: the game didn't start rotating after %d presses", kMaxPressAttempts);
    }
}

void SetMouselook(bool on)
{
    if (g_mouselook.exchange(on) != on) {
        LOG_INFO("Mouselook %s", on ? "on" : "off");
        g_press_attempts = 0;
    }
    UpdateMouselook();
}

void ToggleMouselook()
{
    if (g_mouselook && !OpenPanels().empty()) {
        OpenPanels().clear();  // paused for a panel: the key resumes instead of turning it off
        UpdateMouselook();
        return;
    }
    SetMouselook(!g_mouselook);
}

// A panel key (MouselookPauseKeys) was pressed: it opens or closes that panel, and mouselook pauses
// while any panel opened this way is open.
void OnPanelKey(int key)
{
    if (!g_mouselook) {
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

// While the mod holds the rotate input, the player's own press of it doesn't reach the game: its
// release would end the rotation. Returns true if the game shouldn't get the event.
bool SwallowRealRotateInput(bool mouse_button, int code, bool down)
{
    if (!IsRotateInput(mouse_button, code)) {
        return false;
    }
    if (down) {
        g_real_rotate_swallowed = g_real_rotate_swallowed || g_rotate_held;
        return g_real_rotate_swallowed;
    }
    const bool swallowed = g_real_rotate_swallowed;
    g_real_rotate_swallowed = false;
    return swallowed;
}

// Called by the game when its menu opens or closes, for each player.
using GameMenuFn = void (*)(void* game_ui, int16_t player, bool open);
GameMenuFn g_on_game_menu_changed = nullptr;

void GameMenuHook(void* game_ui, int16_t player, bool open)
{
    // Not seen in the first test: the Esc menu paused mouselook by the movement input alone.
    LOG_INFO("Game menu %s (player %d)", open ? "opened" : "closed", int(player));
    g_game_menu_open = open;
    g_on_game_menu_changed(game_ui, player, open);
}

// Called by the game, on its own thread, when the player's character joins or leaves a dialog. All
// eight argument registers are passed on.
using AnalyticsFn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
AnalyticsFn g_record_dialog_start = nullptr;
AnalyticsFn g_record_dialog_end = nullptr;

uint64_t DialogStartHook(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6,
    uint64_t a7)
{
    LOG_INFO("Dialog started");
    g_dialog_active = true;
    return g_record_dialog_start(a0, a1, a2, a3, a4, a5, a6, a7);
}

uint64_t DialogEndHook(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6,
    uint64_t a7)
{
    LOG_INFO("Dialog ended");
    g_dialog_active = false;
    return g_record_dialog_end(a0, a1, a2, a3, a4, a5, a6, a7);
}

void InstallMouselook(const uintptr_t* addresses)
{
    // The game menu: OnIsGameMenuOpenChanged in the GameUI vtable, found by its target.
    const uintptr_t vtable = addresses[symbols::GameUIVtable];
    const uintptr_t menu = addresses[symbols::OnIsGameMenuOpenChanged];
    bool menu_hooked = false;
    for (size_t i = 0; vtable && menu && i < symbols::kGameUIVtableSlots && !menu_hooked; ++i) {
        const uintptr_t slot = vtable + symbols::kVtableAddressPoint + 8 * i;
        if (*reinterpret_cast<const uintptr_t*>(slot) == menu) {
            g_on_game_menu_changed = reinterpret_cast<GameMenuFn>(menu);
            menu_hooked = WritePointer(slot, reinterpret_cast<uintptr_t>(&GameMenuHook));
        }
    }
    if (!menu_hooked) {
        LOG_WARN("Mouselook won't see the game menu: gui::GameUI::OnIsGameMenuOpenChanged isn't in its vtable");
    }
    const uintptr_t start = addresses[symbols::RecordDialogStart];
    const uintptr_t end = addresses[symbols::RecordDialogEnd];
    const bool dialogs_hooked = start && end &&
        InlineHook("RecordDialogEnd", end, reinterpret_cast<uintptr_t>(&DialogEndHook),
            reinterpret_cast<void**>(&g_record_dialog_end)) &&
        InlineHook("RecordDialogStart", start, reinterpret_cast<uintptr_t>(&DialogStartHook),
            reinterpret_cast<void**>(&g_record_dialog_start));
    if (!dialogs_hooked) {
        LOG_WARN("Mouselook won't see dialogs: ecl::GameAnalyticsSystem::RecordDialogStart/End not hooked");
    }
    g_mouselook_timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
    if (!g_mouselook_timer) {
        LOG_WARN("Mouselook off: no timer");
        return;
    }
    dispatch_source_set_timer(g_mouselook_timer, dispatch_time(DISPATCH_TIME_NOW, 0), NSEC_PER_SEC / 20,
        NSEC_PER_SEC / 100);
    dispatch_source_set_event_handler(g_mouselook_timer, ^{
        if (g_mouselook || g_rotate_held) {
            UpdateMouselook();
        }
    });
    dispatch_resume(g_mouselook_timer);
    LOG_INFO("Mouselook ready: game menu %s, dialogs %s", menu_hooked ? "hooked" : "not hooked",
        dialogs_hooked ? "hooked" : "not hooked");
}

// --- Unclickable active character -------------------------------------------------------------
//
// PreventClickingActiveCharacter, like PreventClickingActiveCharacterHook on Windows: in Character
// mode the cursor ignores the active character, so a click goes to the ground or object behind it.
// The game's picking skips objects that ls::IsFading reports as fading out; for the active
// character it now reports so when ecl::PickingHelper asks. Other callers (character highlights,
// sound occlusion) get the game's answer. The active character comes from
// gui::GameUI::OnSelectedCharacterChanged.

std::atomic<uint64_t> g_active_character{ 0 };  // its entity handle
struct CodeRange {
    uintptr_t start, end;
};
constexpr int kPickingFunctions = symbols::PickingForEachSelectableTarget - symbols::PickGameObject + 1;
CodeRange g_picking_code[kPickingFunctions] = {};

using ForwardFn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
ForwardFn g_is_fading = nullptr;
ForwardFn g_on_selected_character_changed = nullptr;

bool IsPickingCode(uintptr_t address)
{
    for (const CodeRange& range : g_picking_code) {
        if (address >= range.start && address < range.end) {
            return true;
        }
    }
    return false;
}

// Picking runs on worker threads: only atomics here. Although its name says ID const&, the game's
// optimizer passes the entity handle itself in x0: every caller loads it with `ldr x0, [...]`.
uint64_t IsFadingHook(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6,
    uint64_t a7)
{
    const auto caller = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
    const uint64_t character = g_active_character.load(std::memory_order_relaxed);
    if (!character || a0 != character || !IsPickingCode(caller)) {
        return g_is_fading(a0, a1, a2, a3, a4, a5, a6, a7);
    }
    // What the picking code hears about the active character, logged when it changes.
    static std::atomic<int> last_state{ -1 };
    const bool skip = g_prevent_clicking_character && IsCharacterMode() && !IsControllerMode();
    const uint64_t result = skip ? 1 : g_is_fading(a0, a1, a2, a3, a4, a5, a6, a7);
    const int state = (IsCharacterMode() ? 4 : 0) | (skip ? 2 : 0) | int(result & 1);
    if (last_state.exchange(state) != state) {
        LOG_INFO("Picking asks about the active character in %s mode: %s",
            IsCharacterMode() ? "Character" : "Camera",
            skip ? "skipped by the mod" : (result & 1) ? "the game says it fades out" : "clickable");
    }
    return result;
}

// OnSelectedCharacterChanged(this, short player, ecs::EntityRef const& character, bool): the
// EntityRef starts with the entity handle.
uint64_t SelectedCharacterHook(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5,
    uint64_t a6, uint64_t a7)
{
    if (a2) {
        const uint64_t character = *reinterpret_cast<const uint64_t*>(a2);
        if (g_active_character.exchange(character) != character) {
            LOG_INFO("Active character %#llx (player %d)", (unsigned long long)character, int(int16_t(a1)));
        }
    }
    return g_on_selected_character_changed(a0, a1, a2, a3, a4, a5, a6, a7);
}

void InstallUnclickableCharacter(const macho::Image& image, const uintptr_t* addresses)
{
    int ranges = 0;
    for (int i = 0; i < kPickingFunctions; ++i) {
        const uintptr_t start = addresses[symbols::PickGameObject + i];
        const uintptr_t end = start ? macho::NextSymbolAddress(image, start) : 0;
        if (start && end > start) {
            g_picking_code[i] = { start, end };
            ++ranges;
        }
    }
    const uintptr_t is_fading = addresses[symbols::IsFading];
    const uintptr_t selected = addresses[symbols::OnSelectedCharacterChanged];
    if (!ranges || !is_fading || !selected) {
        LOG_WARN("The active character stays clickable: picking functions not found");
        return;
    }
    if (!InlineHook("OnSelectedCharacterChanged", selected, reinterpret_cast<uintptr_t>(&SelectedCharacterHook),
            reinterpret_cast<void**>(&g_on_selected_character_changed)) ||
        !InlineHook("IsFading", is_fading, reinterpret_cast<uintptr_t>(&IsFadingHook),
            reinterpret_cast<void**>(&g_is_fading))) {
        LOG_WARN("The active character stays clickable");
        return;
    }
    LOG_INFO("Unclickable active character: %d picking functions, %s", ranges,
        g_prevent_clicking_character ? "on" : "off in the config");
}

void OnModeChanged(bool was_character, id application, NSEvent* source)
{
    const bool character = IsCharacterMode();
    if (character == was_character) {
        return;
    }
    LOG_INFO("Movement mode: %s", character ? "character" : "camera");
    if (!character) {
        SetAutoforward(application, source, false);
        EndRotateForward(application, source);
    }
    if (g_mode_toggles_mouselook) {
        SetMouselook(character);
    }
}

// A hotkey's key went down or up; `key` is already in hotkeys' state. Returns true if the game
// shouldn't get the event. The same rules as the Linux port's input.cpp.
bool OnKey(id application, NSEvent* source, const Key& key, bool down)
{
    auto pressed = [&](Command command) { return down && hotkeys::Matches(command, key); };
    auto released = [&](Command command) { return !down && hotkeys::Matches(command, key); };
    bool swallow = false;

    if (g_autoforward && (pressed(hotkeys::Forward) || pressed(hotkeys::Backward) ||
                             pressed(hotkeys::ToggleMovementMode) || pressed(hotkeys::HoldMovementMode) ||
                             released(hotkeys::HoldMovementMode))) {
        // The key the mod holds stays down if the player pressed that same key: its own release
        // ends it. Any other key needs the mod's key released first.
        const bool same_key = pressed(hotkeys::Forward) && key.kind == Key::Scancode &&
            keycodes::VirtualKeyFromHid(key.code) == g_forward_virtual_key;
        g_autoforward = false;
        if (!same_key) {
            SendForwardKey(application, source, false);
        }
        LOG_INFO("Autoforward off");
    }

    if (pressed(hotkeys::ToggleAutoforward) && IsCharacterMode()) {
        SetAutoforward(application, source, !g_autoforward);
        // The game doesn't see the hotkey: with the default Shift+W it would release the key the mod holds.
        if (key.kind == Key::Scancode) {
            g_swallowed_key = key.code;
            swallow = true;
        }
    }

    const bool was_character = IsCharacterMode();
    if (pressed(hotkeys::ToggleMovementMode)) {
        g_mode_toggled = !g_mode_toggled.load();
    } else if (pressed(hotkeys::HoldMovementMode)) {
        g_mode_held = true;
    } else if (released(hotkeys::HoldMovementMode)) {
        g_mode_held = false;
    }
    OnModeChanged(was_character, application, source);

    if (IsCharacterMode()) {
        if (pressed(hotkeys::ToggleWalkspeed)) {
            g_walking_toggled = !g_walking_toggled.load();
            LOG_INFO("Walking %s", g_walking_toggled.load() ? "on" : "off");
        } else if (pressed(hotkeys::HoldWalkspeed)) {
            g_walking_held = true;
        } else if (released(hotkeys::HoldWalkspeed)) {
            g_walking_held = false;
        }
    }

    if (pressed(hotkeys::ReloadConfig)) {
        LoadSettings(false);
    }

    // Panels (inventory, map, ...) keep the game taking movement input, so mouselook can't see them;
    // their keys pause it instead. Esc closes panels.
    if (pressed(hotkeys::MouselookPause)) {
        OnPanelKey(key.kind == Key::MouseButton ? -key.code : key.code);
        UpdateMouselook();
    }
    if (down && key.kind == Key::Scancode && key.code == hotkeys::kEscape && !OpenPanels().empty()) {
        OpenPanels().clear();
        UpdateMouselook();
    }
    if (pressed(hotkeys::ToggleCameraRotation)) {
        ToggleMouselook();
        // On a mouse button, the game would take the click as its middle button, which rotates.
        swallow = swallow || key.kind == Key::MouseButton;
    }
    return swallow;
}

// Keeps Shift, Control, Option and Command, left and right, in step with an event's flags, and
// treats each change as a key press or release.
void SyncModifiers(id application, NSEvent* event, bool typing)
{
    bool down[8];
    keycodes::ModifiersDown(event.modifierFlags, down);
    for (int i = 0; i < 8; ++i) {
        const Key key{ Key::Scancode, hotkeys::kLCtrl + i };
        if (hotkeys::IsDown(key) != down[i]) {
            hotkeys::SetDown(key, down[i]);
            if (!down[i] || !typing) {
                OnKey(application, event, key, down[i]);
            }
        }
    }
}

// The mod numbers mouse buttons like SDL: 1 left, 2 middle, 3 right, 4 x1, 5 x2. NSEvent's
// buttonNumber counts 0 left, 1 right, 2 middle, then the side buttons; the left and right buttons
// come from the event type, which every mouse event has right.
int MouseButton(NSEvent* event)
{
    switch (event.type) {
    case NSEventTypeLeftMouseDown:
    case NSEventTypeLeftMouseUp:
        return 1;
    case NSEventTypeRightMouseDown:
    case NSEventTypeRightMouseUp:
        return 3;
    default: {
        const NSInteger number = event.buttonNumber;
        return number == 2 ? 2 : number == 3 ? 4 : number == 4 ? 5 : -1;
    }
    }
}

// Key events go to the log, except while the player types into a text field.
void LogEvent(NSEvent* event)
{
    const NSEventType type = event.type;
    switch (type) {
    case NSEventTypeKeyDown:
    case NSEventTypeKeyUp:
        if (!event.isARepeat && TakeLine()) {
            LOG_INFO("sendEvent: key %s, keyCode %u, modifiers %#lx", type == NSEventTypeKeyDown ? "down" : "up",
                unsigned(event.keyCode), (unsigned long)event.modifierFlags);
        }
        break;
    case NSEventTypeFlagsChanged:
        if (TakeLine()) {
            LOG_INFO("sendEvent: modifiers %#lx, keyCode %u", (unsigned long)event.modifierFlags,
                unsigned(event.keyCode));
        }
        break;
    default:
        break;
    }
}

// Returns true if the game shouldn't get the event.
bool OnEvent(id application, NSEvent* event)
{
    switch (event.type) {
    case NSEventTypeKeyDown:
    case NSEventTypeKeyUp: {
        // Typing into a text field must not toggle modes or autoforward. Releases still count, so
        // a held key can't get stuck.
        const bool typing = TextInputFocused();
        if (!typing) {
            LogEvent(event);
        }
        SyncModifiers(application, event, typing);
        const int hid = keycodes::HidFromVirtualKey(event.keyCode);
        if (hid <= 0) {
            return false;
        }
        const bool down = event.type == NSEventTypeKeyDown;
        const Key key{ Key::Scancode, hid };
        if (hid == g_swallowed_key) {
            hotkeys::SetDown(key, down);
            if (!down) {
                g_swallowed_key = -1;
            }
            return true;
        }
        hotkeys::SetDown(key, down);
        // Auto-repeat is isARepeat alone: when a key was released in another app, the game never
        // saw its key-up, and its next press must still count.
        if (down && event.isARepeat) {
            return g_real_rotate_swallowed && IsRotateInput(false, hid);
        }
        TrackRotateInput(false, hid, down);
        const bool swallow = !(down && typing) && OnKey(application, event, key, down);
        return SwallowRealRotateInput(false, hid, down) || swallow;
    }
    case NSEventTypeFlagsChanged: {
        const bool typing = TextInputFocused();
        if (!typing) {
            LogEvent(event);
        }
        // The modifiers first: a hotkey on Caps Lock must see the Shift, Control and Option keys
        // that this event says are down.
        SyncModifiers(application, event, typing);
        // Caps Lock is a lock key: macOS reports a change of its state, on press or on release
        // depending on the keyboard. Each change counts as one press and release.
        if (event.keyCode == keycodes::kVirtualCapsLock) {
            const int caps_lock = (event.modifierFlags & NSEventModifierFlagCapsLock) ? 1 : 0;
            if (caps_lock != g_last_caps_lock && !typing) {
                const Key key{ Key::Scancode, hotkeys::kCapsLock };
                hotkeys::SetDown(key, true);
                OnKey(application, event, key, true);
                hotkeys::SetDown(key, false);
                OnKey(application, event, key, false);
            }
            g_last_caps_lock = caps_lock;
        }
        return false;
    }
    case NSEventTypeLeftMouseDown:
    case NSEventTypeLeftMouseUp:
    case NSEventTypeRightMouseDown:
    case NSEventTypeRightMouseUp:
    case NSEventTypeOtherMouseDown:
    case NSEventTypeOtherMouseUp: {
        const int button = MouseButton(event);
        if (button < 0) {
            return false;
        }
        const NSEventType type = event.type;
        const bool down = type == NSEventTypeLeftMouseDown || type == NSEventTypeRightMouseDown ||
            type == NSEventTypeOtherMouseDown;
        const Key key{ Key::MouseButton, button };
        hotkeys::SetDown(key, down);
        if (!down && button == g_swallowed_button) {
            g_swallowed_button = -1;
            return true;
        }
        if (button == 1 && OnLeftButton(application, event, down)) {
            return true;
        }
        TrackRotateInput(true, button, down);
        if (OnKey(application, event, key, down) && down) {
            g_swallowed_button = button;
            return true;
        }
        return SwallowRealRotateInput(true, button, down);
    }
    case NSEventTypeMouseMoved:
    case NSEventTypeLeftMouseDragged:
    case NSEventTypeRightMouseDragged:
    case NSEventTypeOtherMouseDragged:
        OnRotationMouseMoved(event);
        return false;
    default:
        return false;
    }
}

void HookedSendEvent(id self, SEL command, NSEvent* event)
{
    if (event && OnEvent(self, event)) {
        return;
    }
    reinterpret_cast<void (*)(id, SEL, NSEvent*)>(g_original_send_event)(self, command, event);
    if (event) {
        AfterEvent(event);
    }
}

bool SwizzleSendEvent()
{
    Class application = NSClassFromString(@"BG3Application");
    if (!application) {
        return false;
    }
    Method method = class_getInstanceMethod(application, @selector(sendEvent:));
    if (!method) {
        return false;
    }
    g_original_send_event = method_setImplementation(method, reinterpret_cast<IMP>(&HookedSendEvent));
    return g_original_send_event != nullptr;
}

// --- Start ------------------------------------------------------------------------------------

void LogProcess()
{
    char os[32] = "unknown";
    size_t os_size = sizeof os;
    sysctlbyname("kern.osproductversion", os, &os_size, nullptr, 0);
    LOG_INFO("BG3 WASD Everywhere %s for macOS (prototype), pid %d, macOS %s, arm64", BG3WASD_VERSION, getpid(), os);
    char path[PATH_MAX];
    uint32_t path_size = sizeof path;
    if (_NSGetExecutablePath(path, &path_size) == 0) {
        LOG_INFO("Executable: %s", path);
    }
    // macOS asks about privacy permissions on behalf of the responsible process: Steam, when the
    // launcher starts the game.
    using ResponsibleFn = pid_t (*)(pid_t pid);
    if (const auto responsible = reinterpret_cast<ResponsibleFn>(
            dlsym(RTLD_DEFAULT, "responsibility_get_pid_responsible_for_pid"))) {
        const pid_t owner = responsible(getpid());
        char owner_name[256] = "?";
        proc_name(owner, owner_name, sizeof owner_name);
        LOG_INFO("Responsible process: %d (%s)%s", int(owner), owner_name,
            owner == getpid() ? ", the game itself" : "");
    }
    NSBundle* bundle = NSBundle.mainBundle;
    NSString* version = [bundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"];
    NSString* build = [bundle objectForInfoDictionaryKey:@"CFBundleVersion"];
    LOG_INFO("Bundle %s, version %s, build %s", bundle.bundleIdentifier.UTF8String ?: "?",
        version.UTF8String ?: "?", build.UTF8String ?: "?");
}

void Run()
{
    // Appends: the game may run as more than one process, and each line names its pid. The launcher
    // and run-game.sh remove the old log first.
    wasd::log::Open(LogPath().c_str(), true);
    LogProcess();
    ConfigPath() = LibraryDirectory() + "/" + kConfigName;
    LoadSettings(true);

    intptr_t slide = 0;
    const macho::Header64* header = MainImage(slide);
    macho::Image image;
    if (!macho::ImageFromMemory(header, slide, image)) {
        LOG_ERROR("No symbol table in the main executable, stopping");
        return;
    }
    LOG_INFO("Main executable at %p, slide %#lx, %u symbols", static_cast<const void*>(header), (unsigned long)slide,
        image.symbol_count);
    uintptr_t addresses[symbols::Count];
    const int64_t start = NowMs();
    const size_t found = macho::FindSymbols(image, symbols::kNames, addresses, symbols::Count);
    LOG_INFO("Found %zu of %d symbols in %lld ms", found, int(symbols::Count), (long long)(NowMs() - start));
    for (int i = 0; i < symbols::Count; ++i) {
        LOG_INFO("  %#lx (file %#lx) %s", (unsigned long)addresses[i],
            addresses[i] ? (unsigned long)(addresses[i] - slide) : 0UL, symbols::kNames[i]);
    }

    InstallWasdUnlock(image, addresses);
    // The game hasn't read its key bindings yet, so they can be patched on disk now.
    BindMovementKeys();
    ChooseForwardKey();
    InstallCameraInputHook(addresses);
    InstallMovementHook(addresses);
    InstallTextInputCheck(addresses);
    InstallRotationHooks(addresses);
    InstallMouselook(addresses);
    InstallUnclickableCharacter(image, addresses);

    if (SwizzleSendEvent()) {
        LOG_INFO("Hotkeys enabled (-[BG3Application sendEvent:])");
    } else {
        LOG_WARN("BG3Application isn't registered yet, trying again when the main run loop starts");
        dispatch_async(dispatch_get_main_queue(), ^{
            if (SwizzleSendEvent()) {
                LOG_INFO("Hotkeys enabled (-[BG3Application sendEvent:], on the main run loop)");
            } else {
                LOG_ERROR("Could not swizzle -[BG3Application sendEvent:]: no hotkeys");
            }
        });
    }
    WatchSettings();
    LOG_INFO("Ready, movement mode: %s%s", IsCharacterMode() ? "character" : "camera", IsWalking() ? ", walking" : "");
}

// Runs in every process that inherits DYLD_INSERT_LIBRARIES and acts only in the game.
__attribute__((constructor)) void OnLibraryLoad()
{
    if (std::strcmp(getprogname(), kGameName) != 0) {
        return;
    }
    @autoreleasepool {
        Run();
    }
}

}  // namespace

// Called by the stub in stubs.S with the arguments of ecl::CameraSystem::OnInputEvent.
extern "C" void ObserveCameraInput(void* self, const void* entity, const uint8_t* event)
{
    if (!event) {
        return;
    }
    // In Character mode the camera doesn't get the movement keys' camera actions: their id becomes
    // 0, as ExecuteCameraCommandHook does on Windows and on Linux.
    uint32_t action;
    std::memcpy(&action, event, sizeof action);
    if (action >= kFirstCameraMove && action <= kLastCameraMove && IsCharacterMode()) {
        const uint32_t none = 0;
        std::memcpy(const_cast<uint8_t*>(event), &none, sizeof none);
        static std::atomic<int> blocked{ 0 };
        if (blocked.fetch_add(1) < 4) {
            LOG_INFO("Camera movement action %u blocked in Character mode", action);
        }
        return;
    }

    // The rest of ls::InputEvent isn't known yet: the first few events of each id go to the log.
    struct Kind {
        uint32_t id;
        uint32_t count;
    };
    static std::mutex mutex;
    static Kind kinds[64];
    static size_t kind_count = 0;
    static uint64_t calls = 0;
    std::lock_guard lock(mutex);
    if (++calls == 1) {
        LOG_INFO("CameraSystem::OnInputEvent: first call (this %p, entity %p)", self, entity);
    }
    Kind* kind = nullptr;
    for (size_t i = 0; i < kind_count; ++i) {
        if (kinds[i].id == action) {
            kind = &kinds[i];
            break;
        }
    }
    if (!kind) {
        if (kind_count == std::size(kinds)) {
            return;
        }
        kind = &kinds[kind_count++];
        *kind = { action, 0 };
    }
    if (++kind->count > 2 || !TakeLine()) {
        return;
    }
    constexpr size_t kBytes = 40;
    char hex[kBytes * 3 + 1] = {};
    for (size_t i = 0; i < kBytes; ++i) {
        std::snprintf(hex + 3 * i, 4, "%02x ", event[i]);
    }
    LOG_INFO("camera input #%llu, id %u #%u: %s", (unsigned long long)calls, action, kind->count, hex);
}
