#pragma once

#include <array>
#include <memory>
#include <optional>

#include "game/chunk.h"

struct BlockSample
{
    Block block{};
    ChunkCoord owner{};
};

struct ChunkNeighborhood
{
    std::shared_ptr<const ChunkData> center{};
    std::shared_ptr<const ChunkData> north{};
    std::shared_ptr<const ChunkData> south{};
    std::shared_ptr<const ChunkData> east{};
    std::shared_ptr<const ChunkData> west{};
    std::shared_ptr<const ChunkData> northEast{};
    std::shared_ptr<const ChunkData> northWest{};
    std::shared_ptr<const ChunkData> southEast{};
    std::shared_ptr<const ChunkData> southWest{};

    [[nodiscard]] const ChunkData* get_by_offset(int deltaX, int deltaZ) const noexcept;
};

[[nodiscard]] std::optional<BlockSample> sample_block(const ChunkNeighborhood& neighborhood, int localX, int y, int localZ);
