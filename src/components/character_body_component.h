#pragma once

#include <glm/vec3.hpp>

#include "game_object.h"

struct CharacterBodyComponent final : Component
{
    glm::vec3 cameraTargetOffset{0.0f, 1.4f, 0.0f};

    [[nodiscard]] glm::vec3 camera_target(const glm::vec3& position) const noexcept
    {
        return position + cameraTargetOffset;
    }
};
