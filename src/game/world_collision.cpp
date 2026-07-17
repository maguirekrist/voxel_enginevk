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

    return intersects_solid_blocks(bounds, _world->geometry(), [this](const glm::ivec3& worldBlock) -> bool
    {
        const glm::vec3 samplePosition = _world->geometry().voxel_to_world(glm::vec3(worldBlock));
        const Block* const block = _world->get_block(samplePosition);
        return block != nullptr && block->_solid;
    });
}

bool WorldCollision::intersects_solid_blocks(
    const AABB& bounds,
    const std::function<bool(const glm::ivec3&)>& isSolidBlock)
{
    static const WorldGeometry defaultGeometry{};
    return intersects_solid_blocks(bounds, defaultGeometry, isSolidBlock);
}

bool WorldCollision::intersects_solid_blocks(
    const AABB& bounds,
    const WorldGeometry& geometry,
    const std::function<bool(const glm::ivec3&)>& isSolidBlock)
{
    const glm::vec3 minVoxel = geometry.world_to_voxel(bounds.min);
    const glm::vec3 maxVoxel = geometry.world_to_voxel(bounds.max);
    const int minX = min_block_index(minVoxel.x);
    const int minY = min_block_index(minVoxel.y);
    const int minZ = min_block_index(minVoxel.z);
    const int maxX = max_block_index(maxVoxel.x);
    const int maxY = max_block_index(maxVoxel.y);
    const int maxZ = max_block_index(maxVoxel.z);

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
