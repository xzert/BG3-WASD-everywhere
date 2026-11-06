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

struct Vector2 {
    float x, y;
};

int g_events = 0;

// Returned in s0/s1.
Vector2 GetRotatedInput(short player, bool flag)
{
    return { float(player) + 0.5f, flag ? 1.0f : -1.0f };
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
    (void)event;
    ++g_events;
}
@end

int main()
{
    @autoreleasepool {
        int failures = 0;

        ecl::CameraSystem* camera = new ecl::CameraSystem;
        const ecs::EntityRef entity{ 7 };
        for (uint32_t id : { 99u, 100u, 101u, 102u, 5u }) {
            const ls::InputEvent event{ id, 2, 1.0f };
            const ecl::InputResult result = camera->OnInputEvent(entity, event);
            if (result.entity != 7 || result.id != id || result.device != 2 || result.check != 7000 + id) {
                std::printf("OnInputEvent(%u) returned %llu %llu %llu %llu\n", id, (unsigned long long)result.entity,
                    (unsigned long long)result.id, (unsigned long long)result.device,
                    (unsigned long long)result.check);
                ++failures;
            }
        }

        for (short player = 0; player < 3; ++player) {
            const bool flag = player != 1;
            const Vector2 value = GetRotatedInput(player, flag);
            if (value.x != float(player) + 0.5f || value.y != (flag ? 1.0f : -1.0f)) {
                std::printf("GetRotatedInput(%d) returned %f %f\n", player, value.x, value.y);
                ++failures;
            }
        }

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
        if (g_events != 1) {
            std::printf("sendEvent: reached the application %d times\n", g_events);
            ++failures;
        }

        std::printf("stand-in: %s\n", failures ? "FAILED" : "every call returned the right values");
        return failures ? 1 : 0;
    }
}
