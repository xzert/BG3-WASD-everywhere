#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

// Shared by the Linux and macOS ports: how far behind the active character a picking ray that passes
// through it reaches the ground.
namespace wasd::picking {

// The picking ray's start and end, as the game passes them to PhysXScene::RaycastAll.
struct Ray {
    float start[3];
    float end[3];
    bool valid;
};

struct Measure {
    float behind;  // meters on the ground from the character; infinity if the ray never comes down
    float feet;    // the height taken for the character's feet
    float root[3];
    float offset;  // the root's height above the feet
    bool valid;    // the camera's fields look sane
};

// The camera (ecl::GameCameraBehavior, the Native Camera Tweaks CameraObject) orbits its root at +0x18:
// the camera sits at root + direction * zoom, Y up. The root is not at the feet: with the pitch at 0
// the camera is level with it, at the character's shoulders. The point at +0xC4 (Native Camera
// Tweaks' "cameraPos") has the root's X and Z and lies 1.28 m below it for a dwarf: the feet.
inline Measure GroundDistanceBehind(const uint8_t* camera, const Ray& ray)
{
    Measure m{};
    float base[3];
    std::memcpy(m.root, camera + 0x18, sizeof m.root);
    std::memcpy(base, camera + 0xC4, sizeof base);
    m.offset = m.root[1] - base[1];
    m.feet = base[1];
    m.behind = INFINITY;
    m.valid = m.offset > 0.2f && m.offset < 4.0f && std::fabs(base[0] - m.root[0]) < 1.0f &&
              std::fabs(base[2] - m.root[2]) < 1.0f;
    const float d[3] = { ray.end[0] - ray.start[0], ray.end[1] - ray.start[1], ray.end[2] - ray.start[2] };
    if (!m.valid || !(d[1] < -1e-4f)) {
        return m;
    }
    const float t = (m.feet - ray.start[1]) / d[1];
    if (!(t > 0.0f)) {
        return m;
    }
    const float x = ray.start[0] + d[0] * t - m.root[0];
    const float z = ray.start[2] + d[2] * t - m.root[2];
    m.behind = std::sqrt(x * x + z * z);
    return m;
}

// Walk-back protection, with the camera occluders: the ray of WalkablePick, which finds the ground
// a click moves to, starts at the vertical plane through the character, square to the camera's
// horizontal direction, less `margin` (MaxClickDistanceTowardCamera). Ground, stairs and ledges
// between the camera and the character then can't catch a click and send the character back toward
// the camera; the ground at the character's feet stays clickable. A distance along the ray, as for
// the occluders, would miss the ground below a close camera. The occluders' exception (zoom × pitch
// ≥ 300) applies too: from a steep or distant camera the ground below the character on screen is a
// normal click.
//
// When the ray is already below the character's feet at that plane, the cursor is on the ground
// between the camera and the character. Started there, the ray would begin underground and could
// hit a surface below, from which the game picks the nearest walkable spot, often ahead of the
// character. Such a click is cancelled instead: the ray shrinks to its last centimeter, far away,
// and hits nothing, like a click on the sky.
enum class WalkableRay { Moved, Cancelled, NoCamera, CameraException, NotTowardCharacter };

inline WalkableRay WalkableRayStart(
    const uint8_t* camera, const float* start, const float* end, float margin, float moved[3])
{
    if (!camera) {
        return WalkableRay::NoCamera;
    }
    float zoom = 0.0f, pitch = 0.0f, root[3];
    std::memcpy(&zoom, camera + 0x54, sizeof zoom);
    std::memcpy(&pitch, camera + 0x164, sizeof pitch);
    std::memcpy(root, camera + 0x18, sizeof root);
    constexpr float kThreshold = 300.0f;  // as in the occluders
    if (!(zoom > 0.0f) || !(zoom * pitch < kThreshold) || !std::isfinite(root[0]) || !std::isfinite(root[2])) {
        return WalkableRay::CameraException;
    }
    const float to_x = root[0] - start[0], to_z = root[2] - start[2];
    const float distance = std::sqrt(to_x * to_x + to_z * to_z);
    const float d[3] = { end[0] - start[0], end[1] - start[1], end[2] - start[2] };
    // How far the ray moves toward the character's plane per unit of t.
    const float along = distance > 0.01f ? (d[0] * to_x + d[2] * to_z) / distance : 0.0f;
    const float t = along > 0.0f ? (distance - margin) / along : 0.0f;
    if (!(t > 0.0f && t < 1.0f)) {
        return WalkableRay::NotTowardCharacter;
    }
    for (int i = 0; i < 3; ++i) {
        moved[i] = start[i] + d[i] * t;
    }
    // The feet as in GroundDistanceBehind: +0xC4, under the root.
    float feet[3];
    std::memcpy(feet, camera + 0xC4, sizeof feet);
    const float height = root[1] - feet[1];
    if (!(height > 0.2f && height < 4.0f) || !(std::fabs(feet[0] - root[0]) < 1.0f) ||
        !(std::fabs(feet[2] - root[2]) < 1.0f)) {
        return WalkableRay::CameraException;
    }
    constexpr float kBelowFeet = 0.25f;  // slack for uneven ground at the character's plane
    if (moved[1] < feet[1] - kBelowFeet) {
        const float length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        for (int i = 0; i < 3; ++i) {
            moved[i] = end[i] - d[i] / length * 0.01f;
        }
        return WalkableRay::Cancelled;
    }
    return WalkableRay::Moved;
}

}  // namespace wasd::picking
