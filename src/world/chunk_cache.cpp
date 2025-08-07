//
// Created by Maguire Krist on 8/2/25.
//

#include "chunk_cache.h"

std::size_t ChunkCache::get_chunk_index(ChunkCoord coord) const
{
    auto center = (m_radius ^ 2) - (m_width / 2);

    return 0;
}

ChunkCache::ChunkCache(const int view_distance) : m_radius(view_distance), m_width(view_distance * 2 + 1), m_chunks(m_width * m_width)
{
}

ChunkCache::~ChunkCache()
{
}

// std::weak_ptr<Chunk> ChunkCache::get_chunk(ChunkCoord coord) const
// {
//     // Ok, given a coord, we want to get the chunk!
//     // How do we do this?
//
//     //if we are at the world origin {0, 0} it would be just
// }
