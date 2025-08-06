//
// Created by Maguire Krist on 8/2/25.
//

#ifndef CHUNK_CACHE_H
#define CHUNK_CACHE_H
#include "game/chunk.h"


class ChunkCache {
    ChunkCoord m_origin{0, 0};
    int m_radius{};
    int m_width{};
    std::vector<std::shared_ptr<Chunk>> m_chunks;

    [[nodiscard]] std::size_t get_chunk_index(ChunkCoord coord) const;
public:
    explicit ChunkCache(int view_distance);
    ~ChunkCache();

    [[nodiscard]] std::weak_ptr<Chunk> get_chunk(ChunkCoord coord) const;



};



#endif //CHUNK_CACHE_H
