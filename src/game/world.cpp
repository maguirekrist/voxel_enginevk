

#include "world.h"
#include "block.h"
#include "../world/chunk_manager.h"

namespace
{
    [[nodiscard]] int floor_to_int(const float value)
    {
        return static_cast<int>(std::floor(value));
    }

    [[nodiscard]] int chunk_axis_from_world(const float value)
    {
        return floor_to_int(value / static_cast<float>(CHUNK_SIZE));
    }

    [[nodiscard]] int wrap_to_chunk_axis(const int value, const int axisSize)
    {
        const int mod = value % axisSize;
        return mod < 0 ? mod + axisSize : mod;
    }
}

Block* World::get_block(const glm::vec3& worldPos) const
{
    auto localPos = get_local_coordinates(worldPos);
    if (Chunk::is_outside_chunk(localPos))
    {
        return nullptr;
    }

    auto chunk = get_chunk(worldPos);
    if (chunk != nullptr)
    {
        return &chunk->_data->blocks[localPos.x][localPos.y][localPos.z];
    }
    return nullptr;
}

Chunk* World::get_chunk(const glm::vec3 worldPos) const
{
    const auto chunkCoord = get_chunk_coordinates(worldPos);
    if (const auto chunk = _chunkManager.get_chunk(chunkCoord))
    {
        return chunk;
    }

    return nullptr;
}

ChunkCoord World::get_chunk_coordinates(const glm::vec3 &worldPos)
{
    return {
        chunk_axis_from_world(worldPos.x),
        chunk_axis_from_world(worldPos.z)
    };
}

glm::ivec2 World::get_chunk_origin(const glm::vec3 &worldPos)
{
    const auto chunkCords = get_chunk_coordinates(worldPos);
    return {
        chunkCords.x * CHUNK_SIZE,
        chunkCords.z * CHUNK_SIZE
    };
}

glm::ivec3 World::get_local_coordinates(const glm::vec3 &worldPos)
{
    return {
        wrap_to_chunk_axis(floor_to_int(worldPos.x), CHUNK_SIZE),
        floor_to_int(worldPos.y),
        wrap_to_chunk_axis(floor_to_int(worldPos.z), CHUNK_SIZE)
    };
}
