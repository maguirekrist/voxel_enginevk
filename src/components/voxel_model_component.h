#pragma once

#include <string>

#include <glm/ext/quaternion_float.hpp>
#include <glm/vec3.hpp>

#include "game_object.h"

struct VoxelModelComponent final : Component
{
    std::string assetId{};
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    float scale{1.0f};
    bool visible{true};
};
