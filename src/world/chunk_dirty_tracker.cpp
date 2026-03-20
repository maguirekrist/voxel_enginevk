#include "chunk_dirty_tracker.h"

#include <unordered_set>

std::vector<DirtyChunkMark> ChunkDirtyTracker::affected_chunks(const ChunkCoord& ownerCoord, const glm::ivec3& localPos) const
{
    std::unordered_set<ChunkCoord> affected{};
    affected.insert(ownerCoord);

    const bool westEdge = localPos.x == 0;
    const bool eastEdge = localPos.x == CHUNK_SIZE - 1;
    const bool southEdge = localPos.z == 0;
    const bool northEdge = localPos.z == CHUNK_SIZE - 1;

    if (westEdge)
    {
        affected.insert({ ownerCoord.x - 1, ownerCoord.z });
    }
    if (eastEdge)
    {
        affected.insert({ ownerCoord.x + 1, ownerCoord.z });
    }
    if (southEdge)
    {
        affected.insert({ ownerCoord.x, ownerCoord.z - 1 });
    }
    if (northEdge)
    {
        affected.insert({ ownerCoord.x, ownerCoord.z + 1 });
    }

    if (westEdge && southEdge)
    {
        affected.insert({ ownerCoord.x - 1, ownerCoord.z - 1 });
    }
    if (westEdge && northEdge)
    {
        affected.insert({ ownerCoord.x - 1, ownerCoord.z + 1 });
    }
    if (eastEdge && southEdge)
    {
        affected.insert({ ownerCoord.x + 1, ownerCoord.z - 1 });
    }
    if (eastEdge && northEdge)
    {
        affected.insert({ ownerCoord.x + 1, ownerCoord.z + 1 });
    }

    std::vector<DirtyChunkMark> marks{};
    marks.reserve(affected.size());
    for (const ChunkCoord& coord : affected)
    {
        marks.push_back({ .coord = coord });
    }
    return marks;
}
