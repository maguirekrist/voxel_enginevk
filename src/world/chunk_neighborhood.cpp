#include "chunk_neighborhood.h"

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
    if (neighborhood.center == nullptr || !neighborhood.center->has_block_storage())
    {
        return std::nullopt;
    }

    const int chunkVoxelWidth = neighborhood.center->voxelWidth;
    const int chunkVoxelHeight = neighborhood.center->voxelHeight;
    if (y < 0 || y >= chunkVoxelHeight)
    {
        return std::nullopt;
    }

    if (localX >= 0 && localX < chunkVoxelWidth && localZ >= 0 && localZ < chunkVoxelWidth)
    {
        return BlockSample{
            .block = neighborhood.center->blocks[localX][y][localZ],
            .owner = neighborhood.center->coord
        };
    }

    const int deltaX = localX < 0 ? -1 : (localX >= chunkVoxelWidth ? 1 : 0);
    const int deltaZ = localZ < 0 ? -1 : (localZ >= chunkVoxelWidth ? 1 : 0);
    if (deltaX == 0 && deltaZ == 0)
    {
        return std::nullopt;
    }

    const ChunkData* const neighbor = neighborhood.get_by_offset(deltaX, deltaZ);
    if (neighbor == nullptr || !neighbor->has_block_storage())
    {
        return std::nullopt;
    }

    const auto wrap_axis = [chunkVoxelWidth](const int value)
    {
        const int mod = value % chunkVoxelWidth;
        return mod < 0 ? mod + chunkVoxelWidth : mod;
    };
    const glm::ivec3 wrappedPos{
        wrap_axis(localX),
        y,
        wrap_axis(localZ)
    };
    return BlockSample{
        .block = neighbor->blocks[wrappedPos.x][wrappedPos.y][wrappedPos.z],
        .owner = neighbor->coord
    };
}
