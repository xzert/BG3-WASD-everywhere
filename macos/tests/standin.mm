// A stand-in for the game, to try the probe without starting Baldur's Gate 3. It defines the symbols
// the probe looks for, with the same names and the same vtable slot, calls them the way the game
// does, and checks what they return. tests/run-standin.sh runs it as "Baldur's Gate 3" with the
// probe preloaded. Built without optimization, so every function keeps a full prologue.

#import <AppKit/AppKit.h>

#include <cstdint>
#include <cstdio>
#include <initializer_list>

namespace ecs {
struct EntityRef {
    uint64_t handle;
};
}  // namespace ecs

namespace ls {
struct InputEvent {
    uint32_t id;
    uint32_t device;
    float value;
};
enum EInputPlayerIndex : int { Player1 = 0 };
class InputManager {
public:
    float GetInputValue(const unsigned& id, EInputPlayerIndex player) const;
};
}  // namespace ls

namespace ecl {
// Bigger than 16 bytes, so it comes back through the memory x8 points at: the probe's stubs must
// keep x8 intact.
struct InputResult {
    uint64_t entity, id, device, check;
};

// OnInputEvent sits at 0x40, as in the game: the destructor takes two slots, then six more.
class CameraSystem {
public:
    virtual ~CameraSystem();
    virtual void Slot10();
    virtual void Slot18();
    virtual void Slot20();
    virtual void Slot28();
    virtual void Slot30();
    virtual void Slot38();
    virtual InputResult OnInputEvent(const ecs::EntityRef& entity, const ls::InputEvent& event);
};
}  // namespace ecl

namespace gui {
class GameUI {
public:
    void OnIsGameMenuOpenChanged(short player, bool open);
};
}  // namespace gui

namespace {

struct Vector3 {
    float x, y, z;
};

// What reached the game's sendEvent:, after the library.
struct Received {
    NSEventType type;
    unsigned short key;
    NSEventModifierFlags modifiers;
};
Received g_received[32];
int g_events = 0;

// Returned in s0-s2, like the game's.
Vector3 GetRotatedInput(short player, bool flag)
{
    return { float(player) + 0.5f, flag ? 1.0f : -1.0f, 0.25f };
}

}  // namespace

ecl::CameraSystem::~CameraSystem() {}
void ecl::CameraSystem::Slot10() {}
void ecl::CameraSystem::Slot18() {}
void ecl::CameraSystem::Slot20() {}
void ecl::CameraSystem::Slot28() {}
void ecl::CameraSystem::Slot30() {}
void ecl::CameraSystem::Slot38() {}

ecl::InputResult ecl::CameraSystem::OnInputEvent(const ecs::EntityRef& entity, const ls::InputEvent& event)
{
    return { entity.handle, event.id, event.device, entity.handle * 1000 + event.id };
}

float ls::InputManager::GetInputValue(const unsigned& id, EInputPlayerIndex player) const
{
    return float(id) + float(player);
}

void gui::GameUI::OnIsGameMenuOpenChanged(short, bool) {}

@interface BG3Application : NSApplication
@end

@implementation BG3Application
// Doesn't call NSApplication: over SSH there is no window server session to talk to.
- (void)sendEvent:(NSEvent*)event
{
    if (g_events < 32) {
        g_received[g_events] = { event.type, event.keyCode,
            event.modifierFlags & (NSEventModifierFlagShift | NSEventModifierFlagCapsLock) };
    }
    ++g_events;
}
@end

int main()
{
    @autoreleasepool {
        int failures = 0;

        ecl::CameraSystem* camera = new ecl::CameraSystem;
        const ecs::EntityRef entity{ 7 };
        // In Character mode, the library's default, the camera movement actions (99-102) reach the
        // camera with id 0.
        auto check_camera = [&](bool character_mode) {
            for (uint32_t id : { 99u, 100u, 101u, 102u, 5u }) {
                ls::InputEvent event{ id, 2, 1.0f };
                const ecl::InputResult result = camera->OnInputEvent(entity, event);
                const uint64_t expected = character_mode && id >= 99 && id <= 102 ? 0 : id;
                if (result.entity != 7 || result.id != expected || result.device != 2 ||
                    result.check != 7000 + expected) {
                    std::printf("%s mode: OnInputEvent(%u) returned %llu %llu %llu %llu\n",
                        character_mode ? "Character" : "Camera", id, (unsigned long long)result.entity,
                        (unsigned long long)result.id, (unsigned long long)result.device,
                        (unsigned long long)result.check);
                    ++failures;
                }
            }
        };
        // GetRotatedInput is inline-hooked: in Camera mode it returns no movement.
        auto check_movement = [&](bool character_mode) {
            for (short player = 0; player < 3; ++player) {
                const bool flag = player != 1;
                const Vector3 value = GetRotatedInput(player, flag);
                const Vector3 expected = character_mode ? Vector3{ float(player) + 0.5f, flag ? 1.0f : -1.0f, 0.25f }
                                                        : Vector3{ 0.0f, 0.0f, 0.0f };
                if (value.x != expected.x || value.y != expected.y || value.z != expected.z) {
                    std::printf("%s mode: GetRotatedInput(%d) returned %f %f %f\n",
                        character_mode ? "Character" : "Camera", player, value.x, value.y, value.z);
                    ++failures;
                }
            }
        };
        check_camera(true);
        check_movement(true);

        const ls::InputManager input;
        const unsigned id = 0xA0;
        if (input.GetInputValue(id, ls::Player1) != 160.0f) {
            std::printf("GetInputValue returned a wrong value\n");
            ++failures;
        }
        gui::GameUI().OnIsGameMenuOpenChanged(0, true);

        NSEvent* key = [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint modifierFlags:0
                                       timestamp:0 windowNumber:0 context:nil characters:@"w"
                     charactersIgnoringModifiers:@"w" isARepeat:NO keyCode:13];
        BG3Application* application = [BG3Application alloc];
        [application sendEvent:key];

        // Caps Lock on, then off: each change switches the movement mode.
        for (NSEventModifierFlags flags : { NSEventModifierFlags(NSEventModifierFlagCapsLock), NSEventModifierFlags(0) }) {
            NSEvent* caps_lock = [NSEvent keyEventWithType:NSEventTypeFlagsChanged location:NSZeroPoint
                                             modifierFlags:flags timestamp:0 windowNumber:0 context:nil
                                                characters:@"" charactersIgnoringModifiers:@"" isARepeat:NO
                                                   keyCode:57];
            [application sendEvent:caps_lock];
            check_camera(flags == 0);
            check_movement(flags == 0);
        }
        // Shift+W toggles autoforward: the game gets a Forward key-down of the library's instead, and
        // neither the press nor its repeat and release. Backward ends autoforward: the game gets the
        // library's Forward key-up, then Backward itself.
        auto send = [&](NSEventType type, unsigned short key, NSEventModifierFlags flags, BOOL repeat) {
            NSString* text = key == 13 ? @"w" : @"s";
            [application sendEvent:[NSEvent keyEventWithType:type location:NSZeroPoint modifierFlags:flags
                                                    timestamp:0 windowNumber:0 context:nil characters:text
                                  charactersIgnoringModifiers:text isARepeat:repeat keyCode:key]];
        };
        const int before = g_events;
        send(NSEventTypeKeyDown, 13, NSEventModifierFlagShift, NO);
        send(NSEventTypeKeyDown, 13, NSEventModifierFlagShift, YES);
        send(NSEventTypeKeyUp, 13, NSEventModifierFlagShift, NO);
        send(NSEventTypeKeyDown, 1, 0, NO);
        send(NSEventTypeKeyUp, 1, 0, NO);
        const Received expected[] = { { NSEventTypeKeyDown, 13, 0 }, { NSEventTypeKeyUp, 13, 0 },
            { NSEventTypeKeyDown, 1, 0 }, { NSEventTypeKeyUp, 1, 0 } };
        const int count = int(sizeof expected / sizeof expected[0]);
        bool autoforward_ok = g_events - before == count;
        for (int i = 0; autoforward_ok && i < count; ++i) {
            const Received& got = g_received[before + i];
            autoforward_ok = got.type == expected[i].type && got.key == expected[i].key &&
                got.modifiers == expected[i].modifiers;
        }
        if (!autoforward_ok) {
            std::printf("autoforward: the game got %d events:", g_events - before);
            for (int i = before; i < g_events && i < 32; ++i) {
                std::printf(" type %lu key %u flags %#lx;", (unsigned long)g_received[i].type, g_received[i].key,
                    (unsigned long)g_received[i].modifiers);
            }
            std::printf("\n");
            ++failures;
        }
        if (g_events != 3 + count) {
            std::printf("sendEvent: reached the application %d times\n", g_events);
            ++failures;
        }

        std::printf("stand-in: %s\n", failures ? "FAILED" : "every call returned the right values");
        return failures ? 1 : 0;
    }
}
