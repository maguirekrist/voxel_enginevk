

#include "world.h"
#include "block.h"
#include "../world/chunk_manager.h"

Block* World::get_block(const glm::vec3& worldPos) const
{
    auto localPos = get_local_coordinates(worldPos, geometry());
    if (Chunk::is_outside_chunk(localPos, geometry().chunk_voxel_width(), geometry().chunk_voxel_height()))
    {
        return nullptr;
    }

    auto chunk = get_chunk(worldPos);
    if (chunk != nullptr && chunk->_data != nullptr && chunk->_data->has_block_storage())
    {
        return &chunk->_data->blocks[localPos.x][localPos.y][localPos.z];
    }
    return nullptr;
}

Chunk* World::get_chunk(const glm::vec3 worldPos) const
{
    const auto chunkCoord = get_chunk_coordinates(worldPos, geometry());
    if (const auto chunk = _chunkManager.get_chunk(chunkCoord))
    {
        return chunk;
    }

    return nullptr;
}

const WorldGeometry& World::geometry() const
{
    return _chunkManager.geometry();
}

ChunkCoord World::get_chunk_coordinates(const glm::vec3 &worldPos)
{
    static const WorldGeometry defaultGeometry{};
    return get_chunk_coordinates(worldPos, defaultGeometry);
}

ChunkCoord World::get_chunk_coordinates(const glm::vec3& worldPos, const WorldGeometry& geometry)
{
    return geometry.world_to_chunk(worldPos);
}

glm::ivec2 World::get_chunk_origin(const glm::vec3 &worldPos)
{
    static const WorldGeometry defaultGeometry{};
    return get_chunk_origin(worldPos, defaultGeometry);
}

glm::ivec2 World::get_chunk_origin(const glm::vec3& worldPos, const WorldGeometry& geometry)
{
    const auto chunkCords = get_chunk_coordinates(worldPos, geometry);
    const glm::ivec3 chunkOrigin = geometry.chunk_voxel_origin(chunkCords);
    return {
        chunkOrigin.x,
        chunkOrigin.z
    };
}

glm::ivec3 World::get_local_coordinates(const glm::vec3 &worldPos)
{
    static const WorldGeometry defaultGeometry{};
    return get_local_coordinates(worldPos, defaultGeometry);
}

glm::ivec3 World::get_local_coordinates(const glm::vec3& worldPos, const WorldGeometry& geometry)
{
    return geometry.world_to_local_voxel(worldPos);
}
