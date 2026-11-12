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

}  // namespace wasd::picking
