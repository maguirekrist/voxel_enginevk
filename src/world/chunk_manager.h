#pragma once

#include "chunk_cache.h"
#include "neighbor_barrier.h"
#include "thread_pool.h"


struct MapRange
{
    int low_x{0};
    int high_x{0};
    int low_z{0};
    int high_z{0};

    MapRange() = default;

    MapRange(const ChunkCoord center, const int viewDistance) :
    low_x(center.x - viewDistance), high_x(center.x + viewDistance), low_z(center.z - viewDistance), high_z(center.z + viewDistance)
    {
    }

    [[nodiscard]] constexpr bool contains(const ChunkCoord& coord) const
    {
        return (coord.x >= low_x &&
            coord.x <= high_x &&
            coord.z >= low_z &&
            coord.z <= high_z);
    }

    [[nodiscard]] constexpr bool is_border(const ChunkCoord& coord) const
    {
        return (coord.x == low_x ||
            coord.x == high_x ||
            coord.z == low_z ||
            coord.z == high_z);
    }
};

class ChunkManager {
public:
    std::unique_ptr<ChunkCache> m_chunkCache;

    ChunkManager();
    ~ChunkManager();

    void update();
    void update_player_position(int x, int z);
    Chunk* get_chunk(ChunkCoord coord) const;
    std::optional<std::array<std::shared_ptr<const ChunkData>, 8>> get_chunk_neighbors(ChunkCoord coord) const;

private:
    void initialize_map(MapRange mapRange);
    void schedule_generate(Chunk* chunk, uint32_t gen);
    void schedule_mesh(Chunk* chunk, uint32_t gen);

    bool _initialLoad{true};
    int _viewDistance{GameConfig::DEFAULT_VIEW_DISTANCE};
    size_t _maxChunks{GameConfig::MAXIMUM_CHUNKS};
    ChunkCoord _lastPlayerChunk = {0, 0};
    MapRange _mapRange{};

    moodycamel::BlockingConcurrentQueue<Chunk*> _readyChunks;

    ThreadPool _threadPool{4};
    NeighborBarrier _neighborBarrier;
};