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

// The ground distance from the character to where the ray meets the plane of the camera's root
// position (ecl::GameCameraBehavior +0x18, Windows Camera::cameraRootPos; the root follows the
// character's feet). The game is Y-up. Returns infinity when the ray doesn't come down to that plane.
inline float GroundDistanceBehind(const uint8_t* camera, const Ray& ray, float root_out[3])
{
    float root[3];
    std::memcpy(root, camera + 0x18, sizeof root);
    std::memcpy(root_out, root, sizeof root);
    const float d[3] = { ray.end[0] - ray.start[0], ray.end[1] - ray.start[1], ray.end[2] - ray.start[2] };
    if (!(d[1] < -1e-4f) || !std::isfinite(root[1])) {
        return INFINITY;
    }
    const float t = (root[1] - ray.start[1]) / d[1];
    if (!(t > 0.0f)) {
        return INFINITY;
    }
    const float x = ray.start[0] + d[0] * t - root[0];
    const float z = ray.start[2] + d[2] * t - root[2];
    return std::sqrt(x * x + z * z);
}

}  // namespace wasd::picking
