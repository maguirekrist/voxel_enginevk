#pragma once

#include <functional>

#include <glm/vec3.hpp>

#include "physics/aabb.h"

class World;

class WorldCollision
{
public:
    explicit WorldCollision(World& world) noexcept;

    [[nodiscard]] bool intersects_solid(const AABB& bounds) const;

    [[nodiscard]] static bool intersects_solid_blocks(
        const AABB& bounds,
        const std::function<bool(const glm::ivec3&)>& isSolidBlock);

private:
    World* _world{nullptr};
};
