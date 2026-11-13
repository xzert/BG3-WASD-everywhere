// A stand-in for the game, to try the library without starting Baldur's Gate 3. It defines the
// symbols the library looks for, with the same names and the same vtable slot, calls them the way
// the game does, and checks what they return. It also sends the key and mouse events of the test
// config's hotkeys through -[BG3Application sendEvent:] and checks what reaches the game.
// tests/run-standin.sh runs it as "Baldur's Gate 3" with the library preloaded. Built without
// optimization, so every function keeps a full prologue.
//
// Usage: standin --write-config <config>   writes the test config the library starts with
//        standin <config>                  runs the test; it changes the config on the way

#import <AppKit/AppKit.h>
#include <ApplicationServices/ApplicationServices.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace ecs {
struct EntityRef {
    uint64_t handle;
};
struct EntityHandleTraits;
template <class... Components>
class WorldView {};
}  // namespace ecs

namespace ls {
template <class Traits>
struct ID {
    uint64_t value;
};
struct ConstructionTile;
struct EffectComponent;
struct FadingView0Component;
struct FadingView1Component;
struct InvisibleClimbingHelper;
struct VisualComponent;
using FadingView = ecs::WorldView<const ConstructionTile, const EffectComponent, const FadingView0Component,
    const FadingView1Component, const InvisibleClimbingHelper, const VisualComponent>;
}  // namespace ls

// The game's picking skips objects that fade out; nothing fades in the stand-in. The game's name says
// ID const&, but its optimizer passes the handle itself, so the stand-in does the same under that name.
bool StandinIsFading(uint64_t entity, const ls::FadingView& view, signed char flag) __asm__(
    "__ZN2ls8IsFadingERKNS_2IDIN3ecs18EntityHandleTraitsEEERKNS1_9WorldViewIJKNS_16ConstructionTileEKNS_15EffectComponentEKNS_20FadingView0ComponentEKNS_20FadingView1ComponentEKNS_23InvisibleClimbingHelperEKNS_15VisualComponentEEEEa");

namespace ecl {
class PickingHelper {
public:
    void DoPick();

    uint64_t target = 0;
    bool fading = false;
};
}  // namespace ecl

namespace ls {
struct InputEvent {
    uint32_t id;
    uint32_t device;
    float value;
};
enum EInputPlayerIndex : int { Player1 = 0 };
struct AnyView {
    int id;
};
struct GameTime {
    double time;
};
struct FixedString {
    uint32_t index;
};
class STDString {
public:
    char text[24];
};
class InputManager {
public:
    float GetInputValue(const unsigned& id, EInputPlayerIndex player) const;
};
}  // namespace ls

namespace ecl {
// Bigger than 16 bytes, so it comes back through the memory x8 points at: the library's stubs must
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

namespace ecl {
enum EMouseCursorMode : int {};
// The camera shows its rotation cursor (mode 0x2E) here while the rotate input is held.
class CursorControl {
public:
    void AddForcedMouseCursor(EMouseCursorMode mode);
    void RemoveForcedMouseCursor(EMouseCursorMode mode);

    int added = 0, removed = 0;
};
}  // namespace ecl

namespace json {
class Value {
public:
    int fields = 0;
};
}  // namespace json

namespace ecl {
struct Character;
// Records the start and end of each dialog the player's character joins, and fills the data of
// every analytics event, such as combat_start.
class GameAnalyticsSystem {
public:
    void RecordDialogStart(const Character* character, const char* dialog, const ls::STDString& name, bool flag) const;
    void RecordDialogEnd(const Character* character, const ls::FixedString& dialog, const ls::STDString& name, bool flag) const;
    void FillAllEventsData(json::Value& data, const char* name) const;
};

// A click on the ground: Interact (4) starts a move.
class CharacterTask_MoveTo {
public:
    uint16_t OnInputEvent(const ls::InputEvent& event);

    int moves = 0;
};

// Interact pressed sets the "held" byte at +0x21.
class InputController {
public:
    uint16_t OnInputEvent(const ls::InputEvent& event);

    uint8_t bytes[0x30] = {};
};
}  // namespace ecl

// ecl::CameraSystem::IsEdgePanningAllowed(EntityView<...> const&) const, under the game's name.
bool StandinIsEdgePanningAllowed(const ecl::CameraSystem* system, const void* view) __asm__(
    "__ZNK3ecl12CameraSystem20IsEdgePanningAllowedERKN3ecs8_private10EntityViewIJN2ls8TypeListIJKNS_17EocCamera"
    "BehaviorEKNS_18GameCameraBehaviorEEEENS5_IJKNS_6camera23PlatformTargetComponentEEEEEEE");

// The game's cursor position is the first two floats gCore points to; -[LSMTLView mouseMoved:]
// writes it.
float g_cursor_position[2] = { 400.0f, 300.0f };
float* gCore = g_cursor_position;

namespace gui {
// The engine calls Update every frame through the vtable; the library finds its slot by the target.
class GameUI {
public:
    virtual ~GameUI();
    virtual bool IsTextInputElementFocused() const;
    virtual void Update(const ls::AnyView& view, const ls::GameTime& time);
    virtual void OnIsGameMenuOpenChanged(short player, bool open);
    void OnSelectedCharacterChanged(short player, const ecs::EntityRef& character, bool flag);

    bool text_field_focused = false;
    int updates = 0;
    int menu_changes = 0;
};
}  // namespace gui

namespace {

// The config the test starts with and then changes: ToggleWalkspeed and WalkSpeed vary.
constexpr const char* kConfigFormat = "[ModHotkeys]\n"
                                      "ToggleMovementMode = \"key:capslock\"\n"
                                      "HoldMovementMode = \"mouse:right\"\n"
                                      "ToggleAutoforward = \"shift+key:w\"\n"
                                      "ToggleWalkspeed = \"%s\"\n"
                                      "HoldWalkspeed = \"key:lalt\"\n"
                                      "ReloadConfig = \"ctrl+key:f5\"\n"
                                      "ToggleCameraRotation = \"key:g\"\n"
                                      "MouselookPauseKeys = \"key:i\"\n"
                                      "[Walking]\n"
                                      "WalkSpeed = %s\n"
                                      "[Misc]\n"
                                      "BlockInteractMove = true\n";

// macOS virtual key codes (kVK_*).
constexpr unsigned short kW = 0x0D, kS = 0x01, kR = 0x0F, kT = 0x11, kY = 0x10, kF5 = 0x60;
constexpr unsigned short kCapsLock = 0x39, kOption = 0x3A, kControl = 0x3B;
// NSEvent's device-dependent flags for the left Option and Control keys.
constexpr NSEventModifierFlags kLeftOption = 0x20, kLeftControl = 0x1;

struct Vector3 {
    float x, y, z;
};

// What reached the game's sendEvent:, after the library.
struct Received {
    NSEventType type;
    unsigned short key;
    NSEventModifierFlags modifiers;
};
constexpr int kMaxReceived = 64;
Received g_received[kMaxReceived];
int g_events = 0;
int g_other_downs = 0, g_other_ups = 0;  // middle mouse button presses and releases the game got
int g_dialog_records = 0;
int g_events_filled = 0;
int g_edge_panning_asked = 0;
// While set, the stand-in plays the game's camera: the middle button down shows its rotation cursor.
ecl::CursorControl* g_simulated_camera = nullptr;

// Returned in s0-s2, like the game's.
Vector3 GetRotatedInput(short player, bool flag)
{
    return { float(player) + 0.5f, flag ? 1.0f : -1.0f, 0.25f };
}

bool WriteConfig(const char* path, const char* toggle_walkspeed, const char* walk_speed)
{
    FILE* file = std::fopen(path, "w");
    if (!file) {
        std::printf("can't write %s\n", path);
        return false;
    }
    std::fprintf(file, kConfigFormat, toggle_walkspeed, walk_speed);
    return std::fclose(file) == 0;
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

void ecl::CursorControl::AddForcedMouseCursor(EMouseCursorMode) { ++added; }
void ecl::CursorControl::RemoveForcedMouseCursor(EMouseCursorMode) { ++removed; }

gui::GameUI::~GameUI() {}
bool gui::GameUI::IsTextInputElementFocused() const { return text_field_focused; }
void gui::GameUI::Update(const ls::AnyView& view, const ls::GameTime&) { updates += view.id; }
void gui::GameUI::OnIsGameMenuOpenChanged(short, bool) { ++menu_changes; }
void gui::GameUI::OnSelectedCharacterChanged(short, const ecs::EntityRef&, bool) {}

namespace {
const ls::FadingView g_fading_view;
int g_is_fading_calls = 0;

// IsFading asked from outside the picking code, like the game's character highlights.
bool IsFadingElsewhere(uint64_t entity)
{
    return StandinIsFading(entity, g_fading_view, 0);
}
}  // namespace

bool StandinIsFading(uint64_t, const ls::FadingView&, signed char)
{
    ++g_is_fading_calls;
    return false;
}

void ecl::PickingHelper::DoPick()
{
    fading = StandinIsFading(target, g_fading_view, 0);
}

void ecl::GameAnalyticsSystem::RecordDialogStart(const Character*, const char*, const ls::STDString&, bool) const
{
    ++g_dialog_records;
}

void ecl::GameAnalyticsSystem::RecordDialogEnd(const Character*, const ls::FixedString&, const ls::STDString&, bool) const
{
    ++g_dialog_records;
}

void ecl::GameAnalyticsSystem::FillAllEventsData(json::Value& data, const char* name) const
{
    data.fields += name ? 1 : 0;
    ++g_events_filled;
}

uint16_t ecl::CharacterTask_MoveTo::OnInputEvent(const ls::InputEvent& event)
{
    if (event.id == 4) {
        ++moves;
    }
    return 0;
}

uint16_t ecl::InputController::OnInputEvent(const ls::InputEvent& event)
{
    if (event.id == 4) {
        bytes[0x21] = 1;
    }
    return 0x101;
}

bool StandinIsEdgePanningAllowed(const ecl::CameraSystem* system, const void* view)
{
    // The counter afterwards: its adrp mustn't be among the 4 instructions an inline hook moves.
    const bool allowed = system && view;
    ++g_edge_panning_asked;
    return allowed;
}

@interface BG3Application : NSApplication
@end

@implementation BG3Application
// Doesn't call NSApplication: over SSH there is no window server session to talk to.
- (void)sendEvent:(NSEvent*)event
{
    // Like the game's view: a mouse move writes the cursor position.
    const NSEventType type = event.type;
    if (type == NSEventTypeMouseMoved || type == NSEventTypeLeftMouseDragged || type == NSEventTypeOtherMouseDragged) {
        g_cursor_position[0] = 100.0f;
        g_cursor_position[1] = 100.0f;
    }
    if (type == NSEventTypeOtherMouseDown || type == NSEventTypeOtherMouseUp) {
        const bool down = type == NSEventTypeOtherMouseDown;
        ++(down ? g_other_downs : g_other_ups);
        if (g_simulated_camera) {
            if (down) {
                g_simulated_camera->AddForcedMouseCursor(ecl::EMouseCursorMode(0x2E));
            } else {
                g_simulated_camera->RemoveForcedMouseCursor(ecl::EMouseCursorMode(0x2E));
            }
        }
    }
    if (g_events < kMaxReceived) {
        g_received[g_events] = { event.type, event.type == NSEventTypeKeyDown || event.type == NSEventTypeKeyUp ||
                    event.type == NSEventTypeFlagsChanged ? event.keyCode : (unsigned short)0,
            event.modifierFlags & (NSEventModifierFlagShift | NSEventModifierFlagCapsLock) };
    }
    ++g_events;
}
@end

int main(int argc, char** argv)
{
    if (argc == 3 && std::strcmp(argv[1], "--write-config") == 0) {
        return WriteConfig(argv[2], "key:r", "0.5") ? 0 : 1;
    }
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s [--write-config] <config>\n", argv[0]);
        return 2;
    }
    const char* config = argv[1];

    @autoreleasepool {
        int failures = 0;

        ecl::CameraSystem* camera = new ecl::CameraSystem;
        const ecs::EntityRef entity{ 7 };
        // In Character mode, the camera movement actions (99-102) reach the camera with id 0.
        auto check_camera = [&](bool character_mode, const char* step) {
            for (uint32_t id : { 99u, 100u, 101u, 102u, 5u }) {
                ls::InputEvent event{ id, 2, 1.0f };
                const ecl::InputResult result = camera->OnInputEvent(entity, event);
                const uint64_t expected = character_mode && id >= 99 && id <= 102 ? 0 : id;
                if (result.entity != 7 || result.id != expected || result.device != 2 ||
                    result.check != 7000 + expected) {
                    std::printf("%s: OnInputEvent(%u) returned %llu %llu %llu %llu\n", step, id,
                        (unsigned long long)result.entity, (unsigned long long)result.id,
                        (unsigned long long)result.device, (unsigned long long)result.check);
                    ++failures;
                }
            }
        };
        // GetRotatedInput is inline-hooked: no movement in Camera mode (scale 0), a shorter one while
        // walking.
        auto check_movement = [&](float scale, const char* step) {
            for (short player = 0; player < 3; ++player) {
                const bool flag = player != 1;
                const Vector3 value = GetRotatedInput(player, flag);
                const Vector3 expected{ (float(player) + 0.5f) * scale, (flag ? 1.0f : -1.0f) * scale, 0.25f * scale };
                if (value.x != expected.x || value.y != expected.y || value.z != expected.z) {
                    std::printf("%s: GetRotatedInput(%d) returned %g %g %g, not %g %g %g\n", step, player, value.x,
                        value.y, value.z, expected.x, expected.y, expected.z);
                    ++failures;
                }
            }
        };

        BG3Application* application = [BG3Application alloc];
        int expected_events = 0;
        auto expect_events = [&](int count, const char* step) {
            expected_events += count;
            if (g_events != expected_events) {
                std::printf("%s: sendEvent: reached the game %d times, not %d\n", step, g_events, expected_events);
                ++failures;
                expected_events = g_events;
            }
        };
        auto key = [&](NSEventType type, unsigned short code, NSEventModifierFlags flags, BOOL repeat) {
            NSString* text = code == kW ? @"w" : code == kS ? @"s" : @"";
            [application sendEvent:[NSEvent keyEventWithType:type location:NSZeroPoint modifierFlags:flags
                                                    timestamp:0 windowNumber:0 context:nil characters:text
                                  charactersIgnoringModifiers:text isARepeat:repeat keyCode:code]];
        };
        auto tap = [&](unsigned short code, NSEventModifierFlags flags) {
            key(NSEventTypeKeyDown, code, flags, NO);
            key(NSEventTypeKeyUp, code, flags, NO);
        };
        auto modifiers = [&](unsigned short code, NSEventModifierFlags flags) {
            [application sendEvent:[NSEvent keyEventWithType:NSEventTypeFlagsChanged location:NSZeroPoint
                                               modifierFlags:flags timestamp:0 windowNumber:0 context:nil
                                                  characters:@"" charactersIgnoringModifiers:@"" isARepeat:NO
                                                     keyCode:code]];
        };
        auto mouse = [&](NSEventType type) {
            [application sendEvent:[NSEvent mouseEventWithType:type location:NSZeroPoint modifierFlags:0
                                                      timestamp:0 windowNumber:0 context:nil eventNumber:0
                                                     clickCount:1 pressure:1.0f]];
        };

        // Character mode, the default: the movement keys don't pan the camera, movement is as is.
        check_camera(true, "start");
        check_movement(1.0f, "start");
        const ls::InputManager input;
        const unsigned id = 0xA0;
        if (input.GetInputValue(id, ls::Player1) != 160.0f) {
            std::printf("GetInputValue returned a wrong value\n");
            ++failures;
        }
        gui::GameUI().OnIsGameMenuOpenChanged(0, true);

        tap(kW, 0);
        expect_events(2, "W");

        // Caps Lock on, then off: each change switches the movement mode.
        for (NSEventModifierFlags flags : { NSEventModifierFlags(NSEventModifierFlagCapsLock), NSEventModifierFlags(0) }) {
            modifiers(kCapsLock, flags);
            check_camera(flags == 0, flags ? "Caps Lock on" : "Caps Lock off");
            check_movement(flags == 0 ? 1.0f : 0.0f, flags ? "Caps Lock on" : "Caps Lock off");
        }
        expect_events(2, "Caps Lock");

        // Shift+W toggles autoforward: the game gets a Forward key-down of the library's instead, and
        // neither the press nor its repeat and release. Backward ends autoforward: the game gets the
        // library's Forward key-up, then Backward itself.
        const int before = g_events;
        key(NSEventTypeKeyDown, kW, NSEventModifierFlagShift, NO);
        key(NSEventTypeKeyDown, kW, NSEventModifierFlagShift, YES);
        key(NSEventTypeKeyUp, kW, NSEventModifierFlagShift, NO);
        tap(kS, 0);
        const Received expected[] = { { NSEventTypeKeyDown, kW, 0 }, { NSEventTypeKeyUp, kW, 0 },
            { NSEventTypeKeyDown, kS, 0 }, { NSEventTypeKeyUp, kS, 0 } };
        const int count = int(sizeof expected / sizeof expected[0]);
        bool autoforward_ok = g_events - before == count;
        for (int i = 0; autoforward_ok && i < count; ++i) {
            const Received& got = g_received[before + i];
            autoforward_ok = got.type == expected[i].type && got.key == expected[i].key &&
                got.modifiers == expected[i].modifiers;
        }
        if (!autoforward_ok) {
            std::printf("autoforward: the game got %d events:", g_events - before);
            for (int i = before; i < g_events && i < kMaxReceived; ++i) {
                std::printf(" type %lu key %u flags %#lx;", (unsigned long)g_received[i].type, g_received[i].key,
                    (unsigned long)g_received[i].modifiers);
            }
            std::printf("\n");
            ++failures;
        }
        expect_events(count, "autoforward");

        // Walking at WalkSpeed 0.5: R toggles it, the left Option key holds it.
        tap(kR, 0);
        check_movement(0.5f, "R: walking on");
        tap(kR, 0);
        check_movement(1.0f, "R: walking off");
        modifiers(kOption, NSEventModifierFlagOption | kLeftOption);
        check_movement(0.5f, "Option held");
        modifiers(kOption, 0);
        check_movement(1.0f, "Option released");
        expect_events(6, "walking");

        // The right mouse button holds Camera mode.
        mouse(NSEventTypeRightMouseDown);
        check_movement(0.0f, "right button held");
        check_camera(false, "right button held");
        mouse(NSEventTypeRightMouseUp);
        check_movement(1.0f, "right button released");
        check_camera(true, "right button released");
        expect_events(2, "right button");

        // ReloadConfig (Ctrl+F5) loads the changed config: T toggles walking at 0.25, R no longer.
        failures += WriteConfig(config, "key:t", "0.25") ? 0 : 1;
        modifiers(kControl, NSEventModifierFlagControl | kLeftControl);
        tap(kF5, NSEventModifierFlagControl | kLeftControl);
        modifiers(kControl, 0);
        tap(kR, 0);
        check_movement(1.0f, "R after ReloadConfig");
        tap(kT, 0);
        check_movement(0.25f, "T after ReloadConfig");
        tap(kT, 0);
        check_movement(1.0f, "T again");
        expect_events(10, "ReloadConfig");

        // A saved config is loaded about a second later: Y toggles walking at 0.75.
        failures += WriteConfig(config, "key:y", "0.75") ? 0 : 1;
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:2.5]];
        tap(kY, 0);
        check_movement(0.75f, "Y after saving the config");
        tap(kY, 0);
        check_movement(1.0f, "Y again");
        expect_events(4, "config saved");

        // While the game's GameUI says a text field has the focus, key presses reach the game and
        // trigger no hotkeys: Caps Lock doesn't switch the mode, Shift+W is a capital W.
        gui::GameUI* ui = new gui::GameUI;
        ui->Update(ls::AnyView{ 1 }, ls::GameTime{ 0.0 });
        if (ui->updates != 1) {
            std::printf("GameUI::Update didn't reach the game\n");
            ++failures;
        }
        ui->text_field_focused = true;
        const int typing_start = g_events;
        modifiers(kCapsLock, NSEventModifierFlagCapsLock);
        check_movement(1.0f, "Caps Lock while typing");
        key(NSEventTypeKeyDown, kW, NSEventModifierFlagShift, NO);
        key(NSEventTypeKeyUp, kW, NSEventModifierFlagShift, NO);
        const Received typed[] = { { NSEventTypeFlagsChanged, kCapsLock, NSEventModifierFlagCapsLock },
            { NSEventTypeKeyDown, kW, NSEventModifierFlagShift }, { NSEventTypeKeyUp, kW, NSEventModifierFlagShift } };
        for (int i = 0; i < 3; ++i) {
            const Received& got = g_received[typing_start + i];
            if (typing_start + i >= g_events || got.type != typed[i].type || got.key != typed[i].key ||
                got.modifiers != typed[i].modifiers) {
                std::printf("typing: event %d isn't the one sent\n", i);
                ++failures;
            }
        }
        expect_events(3, "typing");
        // The text field closes: the next Caps Lock change switches the mode again.
        ui->text_field_focused = false;
        modifiers(kCapsLock, 0);
        check_movement(0.0f, "Caps Lock after typing");
        modifiers(kCapsLock, NSEventModifierFlagCapsLock);
        check_movement(1.0f, "Caps Lock again");
        expect_events(2, "after typing");

        // Camera rotation. The game shows its rotation cursor (mode 0x2E) through CursorControl, from
        // its own thread; the library takes the cursor over on the main thread once the mouse moved
        // more than 6 points. Over SSH the real cursor isn't touched, the game's cursor position is.
        ecl::CursorControl* cursors = new ecl::CursorControl;
        auto run_main_queue = [] { [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.2]]; };
        auto cg_mouse = [&](CGEventType type, CGMouseButton button, double dx, double dy) {
            CGEventRef cg = CGEventCreateMouseEvent(nullptr, type, CGPointMake(100, 100), button);
            if (!cg) {
                std::printf("can't create mouse events here\n");
                ++failures;
                return;
            }
            CGEventSetIntegerValueField(cg, kCGMouseEventDeltaX, int64_t(dx));
            CGEventSetIntegerValueField(cg, kCGMouseEventDeltaY, int64_t(dy));
            CGEventSetDoubleValueField(cg, kCGMouseEventDeltaX, dx);
            CGEventSetDoubleValueField(cg, kCGMouseEventDeltaY, dy);
            NSEvent* event = [NSEvent eventWithCGEvent:cg];
            CFRelease(cg);
            if (event && (dx || dy) && event.deltaX != dx) {
                std::printf("mouse event delta %g, not %g\n", event.deltaX, dx);
                ++failures;
            }
            [application sendEvent:event];
        };
        auto check_cursor = [&](float y, const char* step) {
            if (g_cursor_position[1] != y) {
                std::printf("%s: the game's cursor is at y %g, not %g\n", step, g_cursor_position[1], y);
                ++failures;
            }
        };
        cg_mouse(kCGEventOtherMouseDown, kCGMouseButtonCenter, 0, 0);  // the game's rotate input
        cursors->AddForcedMouseCursor(ecl::EMouseCursorMode(0x2E));
        run_main_queue();
        if (cursors->added != 1) {
            std::printf("AddForcedMouseCursor didn't reach the game\n");
            ++failures;
        }
        g_cursor_position[0] = 400.0f;  // where the cursor was when rotation started
        g_cursor_position[1] = 300.0f;
        cg_mouse(kCGEventOtherMouseDragged, kCGMouseButtonCenter, 3, 0);
        check_cursor(100.0f, "a small move while rotating");
        cg_mouse(kCGEventOtherMouseDragged, kCGMouseButtonCenter, 10, 0);
        check_cursor(-1000000.0f, "rotating");
        // Rotate + left click walks forward: the game gets the Forward key instead of the click.
        const int click_start = g_events;
        mouse(NSEventTypeLeftMouseDown);
        mouse(NSEventTypeLeftMouseUp);
        const Received walked[] = { { NSEventTypeKeyDown, kW, 0 }, { NSEventTypeKeyUp, kW, 0 } };
        for (int i = 0; i < 2; ++i) {
            const Received& got = g_received[click_start + i];
            if (click_start + i >= g_events || got.type != walked[i].type || got.key != walked[i].key) {
                std::printf("rotate + left click: event %d isn't the Forward key\n", i);
                ++failures;
            }
        }
        cursors->RemoveForcedMouseCursor(ecl::EMouseCursorMode(0x2E));
        run_main_queue();
        if (cursors->removed != 1) {
            std::printf("RemoveForcedMouseCursor didn't reach the game\n");
            ++failures;
        }
        check_cursor(300.0f, "rotation ended");
        cg_mouse(kCGEventOtherMouseUp, kCGMouseButtonCenter, 0, 0);
        cg_mouse(kCGEventMouseMoved, kCGMouseButtonLeft, 20, 0);
        check_cursor(100.0f, "a move after rotation");
        expect_events(7, "rotation");

        // Another forced cursor, like the ping cursor (7), isn't rotation.
        cursors->AddForcedMouseCursor(ecl::EMouseCursorMode(7));
        run_main_queue();
        cg_mouse(kCGEventMouseMoved, kCGMouseButtonLeft, 20, 0);
        check_cursor(100.0f, "the ping cursor");
        cursors->RemoveForcedMouseCursor(ecl::EMouseCursorMode(7));
        run_main_queue();
        expect_events(1, "the ping cursor");

        // Releasing the rotate input ends rotation even if the game doesn't end it.
        constexpr unsigned short kQ = 0x0C;
        key(NSEventTypeKeyDown, kQ, 0, NO);
        cursors->AddForcedMouseCursor(ecl::EMouseCursorMode(0x2E));
        run_main_queue();
        cg_mouse(kCGEventMouseMoved, kCGMouseButtonLeft, 10, 0);
        check_cursor(-1000000.0f, "rotating with a key");
        key(NSEventTypeKeyUp, kQ, 0, NO);
        check_cursor(100.0f, "the rotate key released");
        cursors->RemoveForcedMouseCursor(ecl::EMouseCursorMode(0x2E));
        run_main_queue();
        expect_events(3, "rotating with a key");

        // Mouselook (ToggleCameraRotation, G in the test config): the library holds the middle button
        // for the player and lets go of it for each pause. The stand-in plays the game, asking for
        // the movement input every 50 ms and showing the rotation cursor while the button is down.
        constexpr unsigned short kG = 0x05, kI = 0x22;
        g_simulated_camera = cursors;
        auto run = [](double seconds) { [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:seconds]]; };
        auto start_frames = [] {
            return [NSTimer scheduledTimerWithTimeInterval:0.05 repeats:YES block:^(NSTimer*) {
                GetRotatedInput(0, true);
            }];
        };
        NSTimer* frames = start_frames();
        const int downs_before = g_other_downs, ups_before = g_other_ups;
        auto expect_presses = [&](int downs, int ups, const char* step) {
            if (g_other_downs - downs_before != downs || g_other_ups - ups_before != ups) {
                std::printf("%s: the game got %d presses and %d releases of the middle button, not %d and %d\n", step,
                    g_other_downs - downs_before, g_other_ups - ups_before, downs, ups);
                ++failures;
            }
        };
        tap(kG, 0);
        run(0.3);
        expect_presses(1, 0, "mouselook on");
        cg_mouse(kCGEventOtherMouseDown, kCGMouseButtonCenter, 0, 0);  // the player's own middle button
        cg_mouse(kCGEventOtherMouseUp, kCGMouseButtonCenter, 0, 0);
        expect_presses(1, 0, "the middle button while mouselook holds it");
        ui->text_field_focused = true;
        run(0.15);
        expect_presses(1, 1, "a text field");
        ui->text_field_focused = false;
        run(0.3);
        expect_presses(2, 1, "the text field closed");
        tap(kI, 0);
        expect_presses(2, 2, "a panel key");
        tap(kI, 0);
        run(0.2);
        expect_presses(3, 2, "the panel key again");
        ui->OnIsGameMenuOpenChanged(0, true);
        run(0.15);
        expect_presses(3, 3, "the game menu");
        ui->OnIsGameMenuOpenChanged(0, false);
        run(0.3);
        expect_presses(4, 3, "the game menu closed");
        if (ui->menu_changes != 2) {
            std::printf("OnIsGameMenuOpenChanged reached the game %d times, not 2\n", ui->menu_changes);
            ++failures;
        }
        const ecl::GameAnalyticsSystem analytics;
        const ls::STDString name{};
        analytics.RecordDialogStart(nullptr, "dialog", name, true);
        run(0.15);
        expect_presses(4, 4, "a dialog");
        analytics.RecordDialogEnd(nullptr, ls::FixedString{ 1 }, name, true);
        run(0.3);
        expect_presses(5, 4, "the dialog ended");
        if (g_dialog_records != 2) {
            std::printf("RecordDialogStart/End reached the game %d times, not 2\n", g_dialog_records);
            ++failures;
        }
        [frames invalidate];  // the game stops asking for the movement input: a menu or a loading screen
        run(0.45);
        expect_presses(5, 5, "no movement input");
        frames = start_frames();
        run(0.3);
        expect_presses(6, 5, "movement input again");
        tap(kG, 0);
        run(0.2);
        expect_presses(6, 6, "mouselook off");
        [frames invalidate];
        g_simulated_camera = nullptr;

        // The active character is unclickable in Character mode: IsFading says it fades out when the
        // picking code asks, and only then.
        ui->OnSelectedCharacterChanged(0, ecs::EntityRef{ 0x1234 }, true);
        ecl::PickingHelper picking;
        auto pick = [&](uint64_t entity) {
            picking.target = entity;
            picking.DoPick();
            return picking.fading;
        };
        const int fading_calls = g_is_fading_calls;
        if (!pick(0x1234) || pick(0x5678) || IsFadingElsewhere(0x1234)) {
            std::printf("unclickable character: the active one is %s, another %s, outside picking %s\n",
                pick(0x1234) ? "skipped" : "NOT skipped", pick(0x5678) ? "SKIPPED" : "not skipped",
                IsFadingElsewhere(0x1234) ? "SKIPPED" : "not skipped");
            ++failures;
        }
        if (g_is_fading_calls - fading_calls != 2) {
            std::printf("IsFading reached the game %d times, not 2\n", g_is_fading_calls - fading_calls);
            ++failures;
        }
        modifiers(kCapsLock, 0);  // Camera mode: the active character is clickable again
        if (pick(0x1234)) {
            std::printf("unclickable character: skipped in Camera mode\n");
            ++failures;
        }
        modifiers(kCapsLock, NSEventModifierFlagCapsLock);
        if (!pick(0x1234)) {
            std::printf("unclickable character: not skipped back in Character mode\n");
            ++failures;
        }

        // Character mode outside combat: no edge panning, and BlockInteractMove (on in the test config)
        // keeps Interact from starting a move, by a click or by holding it.
        ecl::CharacterTask_MoveTo move_to;
        ecl::InputController controller;
        const int edge_asked = g_edge_panning_asked;
        auto check_character_camera = [&](bool active, const char* step) {
            const bool edge = StandinIsEdgePanningAllowed(camera, &entity);
            const int moves = move_to.moves;
            const ls::InputEvent interact{ 4, 1, 1.0f }, other{ 5, 1, 1.0f };
            move_to.OnInputEvent(other);
            const uint16_t moved = move_to.OnInputEvent(interact);
            controller.bytes[0x21] = 0;
            const uint16_t held = controller.OnInputEvent(interact);
            if (edge == active || move_to.moves - moves != (active ? 0 : 1) || moved != 0 ||
                controller.bytes[0x21] != (active ? 0 : 1) || held != 0x101) {
                std::printf("%s: edge panning %s, %d moves, held byte %d, results %u %#x\n", step,
                    edge ? "allowed" : "blocked", move_to.moves - moves, controller.bytes[0x21], moved, held);
                ++failures;
            }
        };
        check_character_camera(true, "Character mode");
        if (g_edge_panning_asked - edge_asked != 0) {
            std::printf("IsEdgePanningAllowed reached the game in Character mode\n");
            ++failures;
        }

        // Combat starts: Camera mode (AutoToggleMovementMode is on by default), everything vanilla.
        const int combat_events = g_events;
        json::Value data;
        analytics.FillAllEventsData(data, "combat_start");
        analytics.FillAllEventsData(data, "combat_start");  // the next combatant
        run(0.2);
        check_movement(0.0f, "combat started");
        check_character_camera(false, "combat started");
        // Character mode in combat: WASD moves the character, the rest stays vanilla.
        modifiers(kCapsLock, 0);
        check_movement(1.0f, "Character mode in combat");
        check_camera(false, "Character mode in combat");
        check_character_camera(false, "Character mode in combat");
        modifiers(kCapsLock, NSEventModifierFlagCapsLock);
        analytics.FillAllEventsData(data, "combat_end");
        run(0.2);
        check_movement(1.0f, "combat ended");
        check_character_camera(true, "combat ended");
        // Turn-based mode outside combat.
        run(0.4);
        analytics.FillAllEventsData(data, "ftb_start");
        run(0.2);
        check_movement(0.0f, "turn-based mode");
        analytics.FillAllEventsData(data, "ftb_end");
        analytics.FillAllEventsData(data, "level_up");
        run(0.2);
        check_movement(1.0f, "turn-based mode ended");
        if (g_events_filled != 6 || data.fields != 6) {
            std::printf("FillAllEventsData reached the game %d times, not 6\n", g_events_filled);
            ++failures;
        }
        if (g_events - combat_events != 2) {
            std::printf("combat: sendEvent: reached the game %d times, not 2\n", g_events - combat_events);
            ++failures;
        }

        std::printf("stand-in: %s\n", failures ? "FAILED" : "every call returned the right values");
        return failures ? 1 : 0;
    }
}
