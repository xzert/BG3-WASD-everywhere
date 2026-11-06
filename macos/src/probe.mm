// Prototype of BG3 WASD Everywhere for the native macOS game on Apple Silicon.
//
// What it does so far:
// - The movement keys move the character. Like WASDUnlockPatch on Windows, it removes the check that
//   keeps the gamepad movement task (ecl::CharacterTask_MoveController) for gamepads.
// - It binds the movement keys in inputconfig_p1.json, like InputconfigPatcher on Windows.
// - Shift+W toggles autoforward in Character mode; Forward or Backward ends it, as in the Linux port.
// - Caps Lock switches between Character and Camera movement mode, as in the Linux port. In Character
//   mode the movement keys don't pan the camera. In Camera mode they only pan it: the movement input
//   that ecl::CharacterTask_MoveController gets (GetRotatedInput, inline-hooked) is zero.
//
// It also keeps the probes of the first macOS build, which only log:
// - The library loads into the game (DYLD_INSERT_LIBRARIES) and finds game functions by name.
// - A vtable slot can be redirected: ecl::CameraSystem::OnInputEvent is observed and passed on.
// - -[BG3Application sendEvent:] can be swizzled: key and mouse button events are logged.

#import <AppKit/AppKit.h>

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

#include <atomic>
#include <cerrno>
#include <chrono>
#include <climits>
#include <initializer_list>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <mutex>
#include <string>

#include "arm64.hpp"
#include "log.hpp"
#include "inputconfig.hpp"
#include "macho.hpp"
#include "patterns.hpp"
#include "symbols.hpp"

#if !defined(__arm64__)
#error "The macOS port supports Apple Silicon (arm64) only."
#endif

// Defined in stubs.S: the pass-through stubs and where each one continues.
extern "C" {
void CameraInputProbe();
extern void* g_camera_input_original;
}

namespace {

using namespace wasd;

constexpr const char* kGameName = "Baldur's Gate 3";
constexpr int kMaxObservedLines = 3000;  // keeps a long session from growing the log without end

// No objects with constructors at namespace scope: see "No static initializers" in CLAUDE.md.
std::atomic<int> g_observed_lines{ 0 };
IMP g_original_send_event = nullptr;

// Character movement mode: the movement keys move the character and don't pan the camera. Camera
// mode puts the game's controller-mode check back, so the keys pan the camera as without the mod.
std::atomic<bool> g_character_mode{ true };
uintptr_t g_controller_mode_flag = 0;  // the game's byte, set while a gamepad is in use
constexpr int kCapsLockKeyCode = 57;         // kVK_CapsLock
constexpr unsigned short kForwardKeyCode = 13;  // kVK_ANSI_W: the W key's position, whatever the layout
constexpr unsigned short kBackwardKeyCode = 1;  // kVK_ANSI_S

// Autoforward holds the Forward key down for the player: the game gets a key-down of its own and,
// when autoforward ends, the key-up. Main thread only, like sendEvent:.
bool g_autoforward = false;
bool g_swallowing_toggle = false;  // the rest of the Shift+W press that toggled it

// The game's camera movement actions: CameraBackward, CameraForward, CameraLeft, CameraRight.
constexpr uint32_t kFirstCameraMove = 99, kLastCameraMove = 102;

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
        LOG_ERROR("Vtable probe: mprotect(read/write) failed: %s", std::strerror(errno));
        return false;
    }
    *reinterpret_cast<volatile uintptr_t*>(slot) = value;
    if (mprotect(page, page_size, PROT_READ) != 0) {
        LOG_WARN("Vtable probe: mprotect(read) failed: %s", std::strerror(errno));
    }
    return true;
}

void InstallVtableProbe(const uintptr_t* addresses)
{
    const uintptr_t vtable = addresses[symbols::CameraSystemVtable];
    const uintptr_t function = addresses[symbols::CameraSystemOnInputEvent];
    if (!vtable || !function) {
        LOG_ERROR("Vtable probe: ecl::CameraSystem symbols not found");
        return;
    }
    const uintptr_t slot = vtable + symbols::kVtableAddressPoint + symbols::kCameraOnInputEventSlot;
    const uintptr_t current = *reinterpret_cast<const uintptr_t*>(slot);
    if (current != function) {
        LOG_ERROR("Vtable probe: slot %#lx holds %#lx, not OnInputEvent %#lx", (unsigned long)slot,
            (unsigned long)current, (unsigned long)function);
        return;
    }
    g_camera_input_original = reinterpret_cast<void*>(current);
    if (WritePointer(slot, reinterpret_cast<uintptr_t>(&CameraInputProbe))) {
        LOG_INFO("Vtable probe: ecl::CameraSystem::OnInputEvent (slot %#lx) now goes through the probe",
            (unsigned long)slot);
    }
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

void CallOriginalSendEvent(id application, NSEvent* event)
{
    reinterpret_cast<void (*)(id, SEL, NSEvent*)>(g_original_send_event)(application, @selector(sendEvent:), event);
}

// Gives the game a Forward key event of the mod's own, for the window `source` went to.
void SendForwardKey(id application, NSEvent* source, bool down)
{
    NSEvent* key = [NSEvent keyEventWithType:down ? NSEventTypeKeyDown : NSEventTypeKeyUp
                                    location:NSZeroPoint
                               modifierFlags:0
                                   timestamp:NSProcessInfo.processInfo.systemUptime
                                windowNumber:source.windowNumber
                                     context:nil
                                  characters:@"w"
                 charactersIgnoringModifiers:@"w"
                                   isARepeat:NO
                                     keyCode:kForwardKeyCode];
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

// Shift+W toggles autoforward, and the game never sees that press. A real Forward or Backward
// ends it: Forward then keeps the key down until its own release, Backward needs the mod's Forward
// released first. Returns true for events the game shouldn't get.
bool HandleAutoforward(id application, NSEvent* event)
{
    const NSEventType type = event.type;
    if (type != NSEventTypeKeyDown && type != NSEventTypeKeyUp) {
        return false;
    }
    const unsigned short key = event.keyCode;
    if (key == kForwardKeyCode && g_swallowing_toggle) {
        if (type == NSEventTypeKeyUp) {
            g_swallowing_toggle = false;
        }
        return true;
    }
    const NSEventModifierFlags modifiers = event.modifierFlags &
        (NSEventModifierFlagShift | NSEventModifierFlagControl | NSEventModifierFlagOption | NSEventModifierFlagCommand);
    if (type == NSEventTypeKeyDown && !event.isARepeat && key == kForwardKeyCode &&
        modifiers == NSEventModifierFlagShift && g_character_mode.load()) {
        g_swallowing_toggle = true;
        SetAutoforward(application, event, !g_autoforward);
        return true;
    }
    if (g_autoforward && type == NSEventTypeKeyDown && !event.isARepeat &&
        (key == kForwardKeyCode || key == kBackwardKeyCode)) {
        g_autoforward = false;
        if (key == kBackwardKeyCode) {
            SendForwardKey(application, event, false);
        }
        LOG_INFO("Autoforward off");
    }
    return false;
}

// Main thread (sendEvent:).
void SetCharacterMode(bool character, id application, NSEvent* source)
{
    if (character == g_character_mode.exchange(character)) {
        return;
    }
    LOG_INFO("Movement mode: %s", character ? "character" : "camera");
    if (!character) {
        SetAutoforward(application, source, false);
    }
}

// (anonymous namespace)::GetRotatedInput(short, bool) returns the movement input turned to the
// camera, a Vector3f in s0-s2. The movement task asks for it before it starts and on every update.
struct Vector3f {
    float x, y, z;
};
using GetRotatedInputFn = Vector3f (*)(int16_t player, bool flag);
GetRotatedInputFn g_get_rotated_input = nullptr;  // the trampoline to the original

// In Camera mode the movement task gets no input, so it doesn't start and stops if it runs, like
// the Linux port's OverrideMovementInput. A gamepad keeps working as without the mod.
Vector3f RotatedInputHook(int16_t player, bool flag)
{
    const Vector3f input = g_get_rotated_input(player, flag);
    const bool gamepad = g_controller_mode_flag && *reinterpret_cast<const volatile uint8_t*>(g_controller_mode_flag);
    if (!g_character_mode.load(std::memory_order_relaxed) && !gamepad) {
        return { 0.0f, 0.0f, 0.0f };
    }
    return input;
}

void InstallMovementHook(const uintptr_t* addresses)
{
    const uintptr_t target = addresses[symbols::GetRotatedInput];
    if (!target) {
        LOG_ERROR("GetRotatedInput not found: Camera mode won't stop the character");
        return;
    }
    InlineHook("GetRotatedInput", target, reinterpret_cast<uintptr_t>(&RotatedInputHook),
        reinterpret_cast<void**>(&g_get_rotated_input));
}

// Caps Lock is a lock key: macOS reports it as a modifier change, on press or on release
// depending on the keyboard. Each change of its state counts as one press.
void OnModifiersChanged(id application, NSEvent* event)
{
    static int last_caps_lock = -1;
    if (event.keyCode != kCapsLockKeyCode) {
        return;
    }
    const int caps_lock = (event.modifierFlags & NSEventModifierFlagCapsLock) ? 1 : 0;
    if (caps_lock != last_caps_lock) {
        last_caps_lock = caps_lock;
        SetCharacterMode(!g_character_mode.load(), application, event);
    }
}

// The game keeps player profiles in ~/Documents on macOS; older builds used Application Support.
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
            inputconfig::PatchProfiles(profiles);
            return;
        }
    }
    LOG_WARN("No player profiles found yet; start the game once, then again to bind the movement keys");
}

void LogEvent(NSEvent* event)
{
    const NSEventType type = event.type;
    switch (type) {
    case NSEventTypeKeyDown:
    case NSEventTypeKeyUp:
        if (TakeLine()) {
            LOG_INFO("sendEvent: key %s, keyCode %u, modifiers %#lx%s", type == NSEventTypeKeyDown ? "down" : "up",
                unsigned(event.keyCode), (unsigned long)event.modifierFlags, event.isARepeat ? ", repeat" : "");
        }
        break;
    case NSEventTypeFlagsChanged:
        if (TakeLine()) {
            LOG_INFO("sendEvent: modifiers %#lx, keyCode %u", (unsigned long)event.modifierFlags,
                unsigned(event.keyCode));
        }
        break;
    case NSEventTypeLeftMouseDown:
    case NSEventTypeLeftMouseUp:
    case NSEventTypeRightMouseDown:
    case NSEventTypeRightMouseUp:
    case NSEventTypeOtherMouseDown:
    case NSEventTypeOtherMouseUp: {
        const bool down = type == NSEventTypeLeftMouseDown || type == NSEventTypeRightMouseDown ||
            type == NSEventTypeOtherMouseDown;
        const NSPoint at = event.locationInWindow;
        if (TakeLine()) {
            LOG_INFO("sendEvent: mouse button %ld %s at %.0f,%.0f", (long)event.buttonNumber, down ? "down" : "up",
                at.x, at.y);
        }
        break;
    }
    default: {
        // Mouse moves, scrolling and the rest: a count every 10 seconds.
        static std::atomic<uint64_t> others{ 0 };
        static std::atomic<int64_t> last_log{ 0 };
        const uint64_t count = others.fetch_add(1) + 1;
        const int64_t now = NowMs();
        if (now - last_log.load() >= 10000) {
            last_log = now;
            if (TakeLine()) {
                LOG_INFO("sendEvent: %llu other events so far (last type %lu)", (unsigned long long)count,
                    (unsigned long)type);
            }
        }
        break;
    }
    }
}

void HookedSendEvent(id self, SEL command, NSEvent* event)
{
    if (event) {
        LogEvent(event);
        if (event.type == NSEventTypeFlagsChanged) {
            OnModifiersChanged(self, event);
        }
        if (HandleAutoforward(self, event)) {
            return;
        }
    }
    reinterpret_cast<void (*)(id, SEL, NSEvent*)>(g_original_send_event)(self, command, event);
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

void Run()
{
    // Appends: the game may run as more than one process (its launcher), and each line names its pid.
    // run-game.sh removes the old log first.
    wasd::log::Open(LogPath().c_str(), true);
    char os[32] = "unknown";
    size_t os_size = sizeof os;
    sysctlbyname("kern.osproductversion", os, &os_size, nullptr, 0);
    LOG_INFO("BG3 WASD Everywhere %s for macOS (prototype), pid %d, macOS %s, arm64", BG3WASD_VERSION, getpid(), os);
    char path[PATH_MAX];
    uint32_t path_size = sizeof path;
    if (_NSGetExecutablePath(path, &path_size) == 0) {
        LOG_INFO("Executable: %s", path);
    }
    // macOS asks about privacy permissions on behalf of the responsible process. Started by Steam
    // through the launcher, the game should be responsible for itself, as when opened normally.
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
    InstallVtableProbe(addresses);

    if (SwizzleSendEvent()) {
        LOG_INFO("Swizzled -[BG3Application sendEvent:]");
    } else {
        LOG_WARN("BG3Application isn't registered yet, trying again when the main run loop starts");
        dispatch_async(dispatch_get_main_queue(), ^{
            if (SwizzleSendEvent()) {
                LOG_INFO("Swizzled -[BG3Application sendEvent:] (on the main run loop)");
            } else {
                LOG_ERROR("Could not swizzle -[BG3Application sendEvent:]");
            }
        });
    }

    InstallMovementHook(addresses);
    LOG_INFO("Probe ready: keys, mouse buttons and camera input events are logged from here on");
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

// Called by the stubs in stubs.S with the hooked functions' arguments.
extern "C" void ObserveCameraInput(void* self, const void* entity, const uint8_t* event)
{
    // The layout of ls::InputEvent isn't known yet; this log is meant to show it. Events are grouped
    // by their first 4 bytes, and the first few of each group are written out in full.
    struct Kind {
        uint32_t key;
        uint32_t count;
    };
    static std::mutex mutex;
    static Kind kinds[64];
    static size_t kind_count = 0;
    static uint64_t calls = 0;

    // In Character mode the camera doesn't get the movement keys' camera actions: their id becomes
    // 0, as ExecuteCameraCommandHook does on Windows and on Linux.
    if (event && g_character_mode.load(std::memory_order_relaxed)) {
        uint32_t action;
        std::memcpy(&action, event, sizeof action);
        if (action >= kFirstCameraMove && action <= kLastCameraMove) {
            const uint32_t none = 0;
            std::memcpy(const_cast<uint8_t*>(event), &none, sizeof none);
            static std::atomic<int> blocked{ 0 };
            if (blocked.fetch_add(1) < 4) {
                LOG_INFO("Camera movement action %u blocked in Character mode", action);
            }
            return;
        }
    }

    std::lock_guard lock(mutex);
    if (++calls == 1) {
        LOG_INFO("CameraSystem::OnInputEvent: first call (this %p, entity %p, event %p)", self, entity,
            static_cast<const void*>(event));
    }
    if (!event) {
        return;
    }
    uint32_t key;
    std::memcpy(&key, event, sizeof key);
    Kind* kind = nullptr;
    for (size_t i = 0; i < kind_count; ++i) {
        if (kinds[i].key == key) {
            kind = &kinds[i];
            break;
        }
    }
    if (!kind) {
        if (kind_count == std::size(kinds)) {
            return;
        }
        kind = &kinds[kind_count++];
        *kind = { key, 0 };
    }
    if (++kind->count > 3 || !TakeLine()) {
        return;
    }
    constexpr size_t kBytes = 40;
    char hex[kBytes * 3 + 1] = {};
    for (size_t i = 0; i < kBytes; ++i) {
        std::snprintf(hex + 3 * i, 4, "%02x ", event[i]);
    }
    LOG_INFO("camera input #%llu, kind %08x #%u: %s", (unsigned long long)calls, key, kind->count, hex);
}

