#pragma once

#include <barrier>
#include <chunk.h>
#include <cstddef>
#include <memory>
#include <utils/blockingconcurrentqueue.h>

class VulkanEngine;

struct WorldUpdateJob {
    int _changeX;
    int _changeZ;
    std::queue<ChunkCoord> _chunksToLoad;
    std::queue<ChunkCoord> _chunksToUnload; 
    std::queue<ChunkCoord> _chunksToMesh;
};

class ChunkManager {
public:
    std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>> _loadedChunks;

    std::vector<std::unique_ptr<Chunk>> _chunks;

    std::vector<RenderObject> _renderChunks;
    std::unordered_set<ChunkCoord> _worldChunks;
    std::unordered_set<ChunkCoord> _oldWorldChunks;
    bool _initLoad{true};

    ChunkManager(VulkanEngine& renderer);

    ~ChunkManager();

    void updatePlayerPosition(int x, int z);

private:
    void updateWorldState();
    void queueWorldUpdate(int changeX, int changeZ);
    void worldUpdate();
    void meshChunk(int threadId);

    std::optional<std::array<Chunk*, 8>> get_chunk_neighbors(ChunkCoord coord);

    int get_chunk_index(ChunkCoord coord);
    void add_chunk(ChunkCoord coord, std::unique_ptr<Chunk>&& chunk);
    Chunk* get_chunk(ChunkCoord coord);

    bool _updatingWorldState = false;
    int _viewDistance;
    size_t _maxChunks;
    size_t _maxThreads;
    ChunkCoord _lastPlayerChunk = {0, 0};

    TerrainGenerator& _terrainGenerator;
    VulkanEngine& _renderer;

    std::vector<std::unique_ptr<Chunk>> _chunkPool;

    std::queue<WorldUpdateJob> _worldUpdateQueue;
    moodycamel::BlockingConcurrentQueue<Chunk*> _chunkGenQueue{_maxChunks};
    moodycamel::BlockingConcurrentQueue<std::pair<Chunk*, std::array<Chunk*, 8>>> _chunkMeshQueue;

    std::vector<std::thread> _workers;
    std::thread _updateThread;

    std::mutex _mutexWorld;
    std::mutex _mutexWork;

    std::condition_variable _cvWorld;
    std::condition_variable _cvWork;
    std::atomic<bool> _workComplete;

    std::barrier<> _sync_point;

    std::atomic<bool> _running;
};