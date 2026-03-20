#pragma once

#include <memory>

#include "chunk_neighborhood.h"

class ChunkLighting
{
public:
    [[nodiscard]] static std::shared_ptr<ChunkData> solve_skylight(const ChunkNeighborhood& neighborhood);
};
