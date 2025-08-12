//
// Created by Maguire Krist on 8/2/25.
//

#ifndef CHUNK_CACHE_H
#define CHUNK_CACHE_H
#include "game/chunk.h"


class ChunkCache {
    int m_radius{};
    int m_width{};
    [[nodiscard]] std::optional<std::size_t> get_chunk_index(ChunkCoord coord) const;

    std::vector<Chunk*> slide_east();
    std::vector<Chunk*> slide_west();
    std::vector<Chunk*> slide_north();
    std::vector<Chunk*> slide_south();

    //@brief confines v to the range of [0, n - 1] even when v is negative....
    static int wrap(int v, int n)
    {
        v %= n;
        if (v < 0) { v += n; }
        return v;
    }
public:
    ChunkCoord m_origin{0, 0};
    int m_origin_buf_x{0};
    int m_origin_buf_z{0};
    std::vector<std::unique_ptr<Chunk>> m_chunks{};

    explicit ChunkCache(int view_distance);
    ~ChunkCache();

    ChunkCache(ChunkCache&&) noexcept = default;
    ChunkCache& operator=(ChunkCache&&) noexcept = default;

    ChunkCache(const ChunkCache&) = delete;
    ChunkCache& operator=(const ChunkCache&) = delete;

    [[nodiscard]] Chunk* get_chunk(ChunkCoord coord) const;
    std::vector<Chunk*> slide(ChunkCoord delta);
};



#endif //CHUNK_CACHE_H
