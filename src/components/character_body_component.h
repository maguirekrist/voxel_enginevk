#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "game_object.h"
#include "physics/aabb.h"

struct CharacterBodyComponent final : Component
{
    glm::vec2 collisionHalfExtents{0.35f, 0.35f};
    float collisionHeight{1.8f};
    glm::vec3 cameraTargetOffset{0.0f, 1.4f, 0.0f};

    [[nodiscard]] AABB world_bounds(const glm::vec3& position) const noexcept
    {
        return AABB{
            .min = position + glm::vec3(-collisionHalfExtents.x, 0.0f, -collisionHalfExtents.y),
            .max = position + glm::vec3(collisionHalfExtents.x, collisionHeight, collisionHalfExtents.y)
        };
    }

    [[nodiscard]] glm::vec3 camera_target(const glm::vec3& position) const noexcept
    {
        return position + cameraTargetOffset;
    }
};
