#include "world_collision.h"

#include <cmath>

#include "game/world.h"

namespace
{
    constexpr float CollisionEpsilon = 0.0001f;

    [[nodiscard]] int min_block_index(const float value) noexcept
    {
        return static_cast<int>(std::floor(value + CollisionEpsilon));
    }

    [[nodiscard]] int max_block_index(const float value) noexcept
    {
        return static_cast<int>(std::floor(value - CollisionEpsilon));
    }
}

WorldCollision::WorldCollision(World& world) noexcept :
    _world(&world)
{
}

bool WorldCollision::intersects_solid(const AABB& bounds) const
{
    if (_world == nullptr)
    {
        return false;
    }

    return intersects_solid_blocks(bounds, [this](const glm::ivec3& worldBlock) -> bool
    {
        const Block* const block = _world->get_block(glm::vec3(
            static_cast<float>(worldBlock.x),
            static_cast<float>(worldBlock.y),
            static_cast<float>(worldBlock.z)));
        return block != nullptr && block->_solid;
    });
}

bool WorldCollision::intersects_solid_blocks(
    const AABB& bounds,
    const std::function<bool(const glm::ivec3&)>& isSolidBlock)
{
    const int minX = min_block_index(bounds.min.x);
    const int minY = min_block_index(bounds.min.y);
    const int minZ = min_block_index(bounds.min.z);
    const int maxX = max_block_index(bounds.max.x);
    const int maxY = max_block_index(bounds.max.y);
    const int maxZ = max_block_index(bounds.max.z);

    for (int x = minX; x <= maxX; ++x)
    {
        for (int y = minY; y <= maxY; ++y)
        {
            for (int z = minZ; z <= maxZ; ++z)
            {
                if (isSolidBlock(glm::ivec3{x, y, z}))
                {
                    return true;
                }
            }
        }
    }

    return false;
}
