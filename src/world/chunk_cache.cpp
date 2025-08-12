//
// Created by Maguire Krist on 8/2/25.
//

#include "chunk_cache.h"


std::optional<std::size_t>
ChunkCache::get_chunk_index(const ChunkCoord coord) const
{
    const int rx = coord.x - m_origin.x;
    const int rz = coord.z - m_origin.z;

    // outside the ring?
    if (std::abs(rx) > m_radius || std::abs(rz) > m_radius)
        return std::nullopt;

    // map relative offset into ring buffer indices
    const int buf_x = wrap(m_origin_buf_x + rx, m_width);
    const int buf_z = wrap(m_origin_buf_z + rz, m_width);

    const std::size_t idx = static_cast<std::size_t>(buf_z + (buf_x * m_width));

    if (auto* chunk = m_chunks[idx].get(); chunk && chunk->_chunkCoord == coord)
        return idx;

    return std::nullopt;
}

std::vector<Chunk*> ChunkCache::slide_east()
{
    std::vector<Chunk*> chunks;
    const auto origin_col = m_origin.z + m_radius;
    const auto left_col = origin_col - m_radius;
    const auto wrapped_index_col = wrap(left_col, m_width);
    for (auto i = 0; i < m_width; ++i)
    {
        auto chunkIndex = wrapped_index_col + (i * m_width);
        auto chunk = m_chunks[chunkIndex].get();
        chunk->reset(ChunkCoord{chunk->_chunkCoord.x, m_origin.z + m_radius + 1});
        chunks.push_back(chunk);
    }
    m_origin.z += 1;
    return chunks;
}

std::vector<Chunk*> ChunkCache::slide_west()
{
    std::vector<Chunk*> chunks;
    const auto origin_col = m_origin.z + m_radius;
    const auto right_col = origin_col + m_radius;
    const auto wrapped_index_col = wrap(right_col, m_width);

    for (auto i = 0; i < m_width; ++i)
    {
        auto chunkIndex = wrapped_index_col + (i * m_width);
        auto chunk = m_chunks[chunkIndex].get();
        chunk->reset(ChunkCoord{chunk->_chunkCoord.x, m_origin.z - m_radius - 1});
        chunks.push_back(chunk);
    }
    m_origin.z -= 1;
    return chunks;
}

std::vector<Chunk*> ChunkCache::slide_north()
{
    std::println("Slide North");
    std::vector<Chunk*> chunks;
    const auto origin_row = m_origin.x + m_radius;
    const auto bottom_row = origin_row - m_radius;
    const auto wrapped_index_row = wrap(bottom_row, m_width);

    for (auto i = 0; i < m_width; ++i)
    {
        auto chunkIndex = i + (wrapped_index_row * m_width);
        auto chunk = m_chunks[chunkIndex].get();
        chunk->reset(ChunkCoord{m_origin.x + m_radius + 1, chunk->_chunkCoord.z});
        chunks.push_back(chunk);
    }
    m_origin.x += 1;
    return chunks;
}

std::vector<Chunk*> ChunkCache::slide_south()
{
    std::println("Slide South");
    std::vector<Chunk*> chunks;
    const auto origin_row = m_origin.x + m_radius;
    const auto top_row = origin_row + m_radius;
    const auto wrapped_index_row = wrap(top_row, m_width);

    for (auto i = 0; i < m_width; ++i)
    {
        auto chunkIndex = i + (wrapped_index_row * m_width);
        auto chunk = m_chunks[chunkIndex].get();
        chunk->reset(ChunkCoord{m_origin.x - m_radius - 1, chunk->_chunkCoord.z});
        chunks.push_back(chunk);
    }

    m_origin.x -= 1;
    return chunks;
}

ChunkCache::ChunkCache(const int view_distance) : m_radius(view_distance), m_width(view_distance * 2 + 1), m_chunks(m_width * m_width)
{
    if (view_distance == 0)
    {
        return;
    }

    for (auto x = -m_radius; x <= m_radius; ++x)
    {
        for (auto z = -m_radius; z <= m_radius; ++z)
        {
            auto x_index = x + m_radius;
            auto z_index = z + m_radius;
            m_chunks[z_index + (x_index * m_width)] = std::make_unique<Chunk>(ChunkCoord{x, z});
        }
    }

    m_origin_buf_x = m_radius;
    m_origin_buf_z = m_radius;
}

ChunkCache::~ChunkCache() = default;

Chunk* ChunkCache::get_chunk(ChunkCoord coord) const
{
    auto chunk_index = get_chunk_index(coord);
    if (chunk_index.has_value())
    {
        return m_chunks[chunk_index.value()].get();
    }
    return nullptr;
}

//we can only slide in 1 direction at a time, so that is 4 directions.
//
std::vector<Chunk*> ChunkCache::slide(ChunkCoord delta)
{
    std::vector<Chunk*> chunks;
    if (delta.x == 1)
    {
        chunks.append_range(slide_north());
    } else if (delta.x == -1)
    {
        chunks.append_range(slide_south());
    }

    if (delta.z == 1)
    {
        chunks.append_range(slide_east());
    } else if (delta.z == -1)
    {
        chunks.append_range(slide_west());
    }

    m_origin_buf_x = wrap(m_origin_buf_x + delta.x, m_width);
    m_origin_buf_z = wrap(m_origin_buf_z + delta.z, m_width);

    return chunks;
}