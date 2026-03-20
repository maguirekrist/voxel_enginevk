#include "chunk_neighborhood.h"

#include "game/world.h"

const ChunkData* ChunkNeighborhood::get_by_offset(const int deltaX, const int deltaZ) const noexcept
{
    if (deltaX == 0 && deltaZ == 1)
    {
        return north.get();
    }
    if (deltaX == 0 && deltaZ == -1)
    {
        return south.get();
    }
    if (deltaX == -1 && deltaZ == 0)
    {
        return east.get();
    }
    if (deltaX == 1 && deltaZ == 0)
    {
        return west.get();
    }
    if (deltaX == -1 && deltaZ == 1)
    {
        return northEast.get();
    }
    if (deltaX == 1 && deltaZ == 1)
    {
        return northWest.get();
    }
    if (deltaX == -1 && deltaZ == -1)
    {
        return southEast.get();
    }
    if (deltaX == 1 && deltaZ == -1)
    {
        return southWest.get();
    }

    return nullptr;
}

std::optional<BlockSample> sample_block(const ChunkNeighborhood& neighborhood, const int localX, const int y, const int localZ)
{
    if (y < 0 || y >= CHUNK_HEIGHT || neighborhood.center == nullptr)
    {
        return std::nullopt;
    }

    if (localX >= 0 && localX < CHUNK_SIZE && localZ >= 0 && localZ < CHUNK_SIZE)
    {
        return BlockSample{
            .block = neighborhood.center->blocks[localX][y][localZ],
            .owner = neighborhood.center->coord
        };
    }

    const int deltaX = localX < 0 ? -1 : (localX >= CHUNK_SIZE ? 1 : 0);
    const int deltaZ = localZ < 0 ? -1 : (localZ >= CHUNK_SIZE ? 1 : 0);
    if (deltaX == 0 && deltaZ == 0)
    {
        return std::nullopt;
    }

    const ChunkData* const neighbor = neighborhood.get_by_offset(deltaX, deltaZ);
    if (neighbor == nullptr)
    {
        return std::nullopt;
    }

    const glm::ivec3 wrappedPos = World::get_local_coordinates(glm::ivec3{ localX, y, localZ });
    return BlockSample{
        .block = neighbor->blocks[wrappedPos.x][wrappedPos.y][wrappedPos.z],
        .owner = neighbor->coord
    };
}
