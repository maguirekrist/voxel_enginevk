#pragma once

#include "chunk_cache.h"
#include "chunk_queue.h"

enum class NeighborStatus
{
    Missing,
    Incomplete,
    Ready,
    Border
};

class ChunkManager {
public:
    //std::unordered_map<ChunkCoord, std::shared_ptr<Chunk>> _chunks;
    ChunkCache m_chunkCache;

    ChunkManager();
    ~ChunkManager();

    void update_player_position(int x, int z);
    Chunk* get_chunk(ChunkCoord coord) const;
    std::optional<std::array<const Chunk*, 8>> get_chunk_neighbors(ChunkCoord coord) const;

private:
    void work_chunk(int threadId);
    void initialize_map(MapRange mapRange);
    NeighborStatus chunk_has_neighbors(ChunkCoord coord) const;

    bool _initialLoad{true};
    int _viewDistance{GameConfig::DEFAULT_VIEW_DISTANCE};
    size_t _maxChunks{GameConfig::MAXIMUM_CHUNKS};
    size_t _maxThreads{1};
    ChunkCoord _lastPlayerChunk = {0, 0};
    MapRange _mapRange{};

    ChunkWorkQueue _chunkWorkQueue;


    std::vector<std::thread> _workers;

    std::atomic<bool> _running{true};
};