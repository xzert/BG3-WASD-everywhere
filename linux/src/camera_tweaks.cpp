#include "camera_tweaks.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>

#include "log.hpp"

// Ported from CameraTweaksManager.cpp and Hooks.cpp of Native Camera Tweaks 2.4.5 by ersh1
// (https://github.com/ersh1/BG3_NativeCameraTweaks, GPL-3.0-or-later). Not ported: the gamepad
// features, the floor check that shortens the zoom when the camera would go under the floor, and
// keeping the zoom on a zone change.
namespace wasd::camera_tweaks {
namespace {

// ecl::GameCameraBehavior fields.
constexpr size_t kCameraFlags = 0xA8;  // bit 0: combat, bit 2: tactical
constexpr uint8_t kFlagCombat = 0x01;
constexpr uint8_t kFlagTactical = 0x04;

// Camera definition fields (RE::CameraDefinition in Native Camera Tweaks).
constexpr size_t kDefinitionSize = 0x190;
constexpr size_t kMaxZoom = 0x28, kMinZoom = 0x2C, kAltMaxZoom = 0x30, kAltMinZoom = 0x34;
constexpr size_t kPitchSpeedA = 0x48, kPitchSpeedB = 0xF0, kPitchSpeedC = 0xF4;
constexpr size_t kHorizontalOffset = 0x64, kVerticalOffset = 0x68;
constexpr size_t kFovClose = 0x84, kFovFar = 0x88, kAltFovClose = 0x8C, kAltFovFar = 0x90;
constexpr size_t kTacticalMinZoom = 0xC8, kTacticalMaxZoom = 0xCC, kTacticalFov = 0xD0;
constexpr size_t kPitchFar = 0x160, kPitchClose = 0x164, kCombatPitchFar = 0x168, kCombatPitchClose = 0x16C;
constexpr size_t kTacticalPitchFar = 0x170, kTacticalPitchClose = 0x174;
constexpr size_t kAltPitchFar = 0x178, kAltPitchClose = 0x17C;
// Everything this file may change; restored from the game's values before new settings apply.
constexpr size_t kTouched[] = { kMaxZoom, kMinZoom, kAltMaxZoom, kAltMinZoom, kPitchSpeedA, kPitchSpeedB,
    kPitchSpeedC, kHorizontalOffset, kVerticalOffset, kFovClose, kFovFar, kAltFovClose, kAltFovFar,
    kTacticalMinZoom, kTacticalMaxZoom, kTacticalFov, kPitchFar, kPitchClose, kCombatPitchFar, kCombatPitchClose,
    kTacticalPitchFar, kTacticalPitchClose, kAltPitchFar, kAltPitchClose };

// With the pitch free, the game must turn the camera to it at once, not at its own pitch speed.
constexpr float kFastPitchSpeed = 100000.0f;

// Camera input ids (ls::InputEvent at +0); the value is a float at +0x14.
constexpr uint32_t kMouseRotateLeft = 109, kMouseRotateRight = 110;
constexpr size_t kInputValue = 0x14;

float ReadFloat(const uint8_t* p)
{
    float value;
    std::memcpy(&value, p, sizeof value);
    return value;
}

void WriteFloat(uint8_t* p, float value) { std::memcpy(p, &value, sizeof value); }

// Settings are replaced, never freed: the game thread may still read the previous ones.
std::atomic<const CameraTweakSettings*> g_settings{ nullptr };
std::atomic<uint32_t> g_generation{ 0 };
GameLayout g_layout;
std::atomic<int> g_mouse_dy{ 0 };

// Game thread only.
enum Kind { Exploration, Combat, Override, KindCount };
struct Snapshot {
    uint8_t* definition = nullptr;
    uint8_t original[kDefinitionSize] = {};
};
Snapshot g_snapshots[KindCount];
uint8_t* g_applied_object = nullptr;
uint32_t g_applied_generation = 0;
bool g_has_pitch = false;
float g_pitch = 0.0f;
int64_t g_last_update_us = 0;

int64_t NowUs()
{
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

uint32_t Offset(Kind kind)
{
    return kind == Exploration ? g_layout.exploration : kind == Combat ? g_layout.combat : g_layout.override_definition;
}

// The definition the camera uses now, as GetCurrentCameraDefinition picks it.
Kind CurrentKind(const uint8_t* object, const uint8_t* camera)
{
    if (g_layout.override_flag && g_layout.override_definition && object[g_layout.override_flag]) {
        return Override;
    }
    return (camera[kCameraFlags] & kFlagCombat) ? Combat : Exploration;
}

void ApplyMode(uint8_t* d, const CameraModeTweaks& m, bool combat)
{
    if (m.override_locked_pitch && (!m.unlock_pitch || m.keep_tactical_pitch_locked)) {
        if (!m.unlock_pitch) {
            WriteFloat(d + (combat ? kCombatPitchClose : kPitchClose), m.locked_pitch_close);
            WriteFloat(d + (combat ? kCombatPitchFar : kPitchFar), m.locked_pitch_far);
            WriteFloat(d + kAltPitchClose, m.locked_alt_pitch_close);
            WriteFloat(d + kAltPitchFar, m.locked_alt_pitch_far);
        }
        WriteFloat(d + kTacticalPitchClose, m.locked_tactical_pitch_close);
        WriteFloat(d + kTacticalPitchFar, m.locked_tactical_pitch_far);
    }
    if (m.override_zoom) {
        WriteFloat(d + kMinZoom, m.zoom_min);
        WriteFloat(d + kMaxZoom, m.zoom_max);
        WriteFloat(d + kTacticalMinZoom, m.tactical_zoom_min);
        WriteFloat(d + kTacticalMaxZoom, m.tactical_zoom_max);
        WriteFloat(d + kAltMinZoom, m.alt_zoom_min);
        WriteFloat(d + kAltMaxZoom, m.alt_zoom_max);
    }
    if (m.override_fov) {
        WriteFloat(d + kFovClose, m.fov_close);
        WriteFloat(d + kFovFar, m.fov_far);
        WriteFloat(d + kTacticalFov, m.tactical_fov);
        WriteFloat(d + kAltFovClose, m.alt_fov_close);
        WriteFloat(d + kAltFovFar, m.alt_fov_far);
    }
    if (m.override_offset) {
        WriteFloat(d + kVerticalOffset, m.vertical_offset_mult);
        WriteFloat(d + kHorizontalOffset, m.horizontal_offset_mult);
    }
}

// Puts the game's own values back, then the configured ones on top.
void ApplySettings(uint8_t* object, const CameraTweakSettings* s)
{
    for (int kind = 0; kind < KindCount; ++kind) {
        Snapshot& snapshot = g_snapshots[kind];
        if (!Offset(static_cast<Kind>(kind))) {
            continue;  // not found on this platform
        }
        uint8_t* definition = object + Offset(static_cast<Kind>(kind));
        if (snapshot.definition != definition) {
            snapshot.definition = definition;
            std::memcpy(snapshot.original, definition, kDefinitionSize);
        }
        for (size_t offset : kTouched) {
            std::memcpy(definition + offset, snapshot.original + offset, sizeof(float));
        }
    }
    if (s && s->enabled) {
        if (!g_snapshots[Exploration].definition || !g_snapshots[Combat].definition) {
            return;
        }
        ApplyMode(g_snapshots[Exploration].definition, s->exploration, false);
        ApplyMode(g_snapshots[Combat].definition, s->combat, true);
        auto zoom = [](const CameraModeTweaks& m, const Snapshot& snapshot) {
            return m.override_zoom ? std::pair(m.zoom_min, m.zoom_max)
                                   : std::pair(ReadFloat(snapshot.original + kMinZoom), ReadFloat(snapshot.original + kMaxZoom));
        };
        const auto exploration = zoom(s->exploration, g_snapshots[Exploration]);
        const auto combat = zoom(s->combat, g_snapshots[Combat]);
        LOG_INFO("Camera tweaks on: zoom %.1f-%.1f (exploration), %.1f-%.1f (combat); free pitch %s / %s", exploration.first,
            exploration.second, combat.first, combat.second, s->exploration.unlock_pitch ? "on" : "off",
            s->combat.unlock_pitch ? "on" : "off");
    } else {
        LOG_INFO("Camera tweaks off: the game's camera settings are back");
    }
}

void SetFastPitch(Kind kind, bool fast)
{
    const Snapshot& snapshot = g_snapshots[kind];
    if (!snapshot.definition) {
        return;
    }
    for (size_t offset : { kPitchSpeedA, kPitchSpeedB, kPitchSpeedC }) {
        WriteFloat(snapshot.definition + offset, fast ? kFastPitchSpeed : ReadFloat(snapshot.original + offset));
    }
}

// Moves `current` toward `target` by a share of the distance that grows with time and speed.
float InterpTo(float current, float target, float dt, float speed)
{
    const float distance = target - current;
    if (speed <= 0.0f || std::fabs(distance) < 0.01f) {
        return target;
    }
    return current + distance * std::clamp(dt * speed, 0.0f, 1.0f);
}

}  // namespace

void SetGameLayout(const GameLayout& layout) { g_layout = layout; }

void Configure(const CameraTweakSettings& settings)
{
    auto* copy = new CameraTweakSettings(settings);
    for (CameraModeTweaks* m : { &copy->exploration, &copy->combat }) {
        m->unlocked_pitch_min = std::clamp(m->unlocked_pitch_min, -89.0f, 89.0f);
        m->unlocked_pitch_max = std::clamp(m->unlocked_pitch_max, -89.0f, 89.0f);
    }
    g_settings.store(copy, std::memory_order_release);
    g_generation.fetch_add(1, std::memory_order_release);
}

void OnMouseMotion(int dy) { g_mouse_dy.fetch_add(dy, std::memory_order_relaxed); }

float OnUpdatePitch(uint8_t* camera, float game_pitch, bool rotating, bool controller)
{
    const uint32_t generation = g_generation.load(std::memory_order_acquire);
    const CameraTweakSettings* s = g_settings.load(std::memory_order_acquire);
    uint8_t* object = g_layout.settings ? *g_layout.settings : nullptr;
    const int dy = g_mouse_dy.exchange(0, std::memory_order_relaxed);
    if (!object || !camera || !std::isfinite(game_pitch)) {
        return game_pitch;
    }
    if (object != g_applied_object || generation != g_applied_generation) {
        ApplySettings(object, s);
        g_applied_object = object;
        g_applied_generation = generation;
    }
    const Kind kind = CurrentKind(object, camera);
    if (!s || !s->enabled) {
        g_has_pitch = false;
        return game_pitch;
    }

    const int64_t now = NowUs();
    const float dt = g_last_update_us ? std::clamp((now - g_last_update_us) / 1e6f, 0.0f, 0.1f) : 0.0f;
    g_last_update_us = now;

    const uint8_t flags = camera[kCameraFlags];
    const bool combat = flags & kFlagCombat;
    const CameraModeTweaks& m = combat ? s->combat : s->exploration;
    const bool unlocked = m.unlock_pitch && !((flags & kFlagTactical) && m.keep_tactical_pitch_locked) && !controller;

    float pitch = game_pitch;
    if (unlocked) {
        const float min = m.unlocked_pitch_min;
        const float max = std::max(m.unlocked_pitch_min, m.unlocked_pitch_max);
        const float delta = rotating ? dy * (s->invert_mouse_pitch ? -1.0f : 1.0f) * s->mouse_pitch_mult *
                                           s->mouse_camera_rotation_mult
                                     : 0.0f;
        if (!g_has_pitch) {
            g_pitch = std::clamp(s->unlocked_pitch_initial_value, min, max);
            g_has_pitch = true;
            LOG_INFO("Free camera pitch: starts at %.1f, limits %.1f to %.1f", g_pitch, min, max);
        } else if (g_pitch + delta > max) {
            g_pitch = std::min(g_pitch, max);
        } else if (g_pitch + delta < min) {
            g_pitch = std::max(g_pitch, min);
        } else {
            g_pitch += delta;
        }
        // The limits can change (combat, a config reload): reach them smoothly.
        if (g_pitch > max) {
            g_pitch = InterpTo(g_pitch, max, dt, s->unlocked_pitch_clamp_speed);
        } else if (g_pitch < min) {
            g_pitch = InterpTo(g_pitch, min, dt, s->unlocked_pitch_clamp_speed);
        }
        pitch = g_pitch;
    } else if (g_has_pitch) {
        // The pitch is the game's again: turn to it smoothly, then let go.
        g_pitch = InterpTo(g_pitch, game_pitch, dt, s->unlocked_pitch_clamp_speed);
        if (g_pitch == game_pitch) {
            g_has_pitch = false;
        }
        pitch = g_pitch;
    }
    for (int k = 0; k < KindCount; ++k) {
        SetFastPitch(static_cast<Kind>(k), k == kind && (unlocked || g_has_pitch));
    }
    return pitch;
}

CommandSpeed BeforeCameraCommand(uint8_t* command, bool controller)
{
    const CameraTweakSettings* s = g_settings.load(std::memory_order_acquire);
    CommandSpeed speed;
    // The controller multipliers of Native Camera Tweaks aren't ported: the game's speeds there.
    if (!s || !s->enabled || controller) {
        return speed;
    }
    speed.zoom = s->mouse_zoom_mult;
    speed.keyboard_rotation = s->keyboard_camera_rotation_mult;
    uint32_t id = 0;
    if (command) {
        std::memcpy(&id, command, sizeof id);
    }
    if ((id == kMouseRotateLeft || id == kMouseRotateRight) && s->mouse_camera_rotation_mult != 1.0f) {
        WriteFloat(command + kInputValue, ReadFloat(command + kInputValue) * s->mouse_camera_rotation_mult);
    }
    return speed;
}

}  // namespace wasd::camera_tweaks
