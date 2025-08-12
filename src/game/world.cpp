

#include "world.h"
#include "block.h"
#include "../world/chunk_manager.h"

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
        return &chunk->_blocks[localPos.x][localPos.y][localPos.z];
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
        static_cast<int>(worldPos.x) / static_cast<int>(CHUNK_SIZE),
        static_cast<int>(worldPos.z) / static_cast<int>(CHUNK_SIZE)
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
        static_cast<int>(std::floor(worldPos.x)) & (CHUNK_SIZE - 1),
        static_cast<int>(std::floor(worldPos.y)) & (CHUNK_HEIGHT - 1),
        static_cast<int>(std::floor(worldPos.z)) & (CHUNK_SIZE - 1)
    };
}
