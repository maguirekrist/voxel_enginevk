#pragma once

#include <optional>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "game/block.h"
#include "vk_types.h"

namespace voxel::picking
{
    struct Ray
    {
        glm::vec3 origin{0.0f};
        glm::vec3 direction{1.0f, 0.0f, 0.0f};
    };

    struct BoxHit
    {
        float distance{0.0f};
        glm::ivec3 outwardNormal{0};
    };

    [[nodiscard]] std::optional<FaceDirection> face_from_outward_normal(const glm::ivec3& normal);
    [[nodiscard]] Ray build_ray_from_cursor(
        int cursorX,
        int cursorY,
        VkExtent2D viewport,
        const glm::vec3& cameraPosition,
        const glm::mat4& inverseViewProjection);
    [[nodiscard]] std::optional<BoxHit> intersect_ray_box(
        const Ray& ray,
        const glm::vec3& boxMin,
        const glm::vec3& boxMax,
        float maxDistance);
}
