#pragma once

#include <vector>

#include "game/chunk.h"

struct DirtyChunkMark
{
    ChunkCoord coord{};
};

class ChunkDirtyTracker
{
public:
    [[nodiscard]] std::vector<DirtyChunkMark> affected_chunks(const ChunkCoord& ownerCoord, const glm::ivec3& localPos) const;
};
