#pragma once

#include <string>

#include "game_object.h"
#include "physics/aabb.h"

struct SpatialColliderComponent final : Component
{
    bool enabled{true};
    bool valid{false};
    AABB localBounds{
        .min = glm::vec3(0.0f),
        .max = glm::vec3(0.0f)
    };
    std::string diagnostic{};

    [[nodiscard]] AABB world_bounds(const glm::vec3& position) const noexcept
    {
        return localBounds.moved(position);
    }
};
