#pragma once

#include "vk_mesh.h"
#include <barrier>
#include <chunk.h>
#include <memory>
#include <utils/concurrentqueue.h>
#include <utils/blockingconcurrentqueue.h>

class VulkanEngine;

struct WorldUpdateJob {
    int _changeX;
    int _changeZ;
    std::queue<ChunkCoord> _chunksToUnload; 
    std::queue<ChunkCoord> _chunksToMesh;
};

class ChunkManager {
public:
    //Real chunk data
    std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>> _chunks;

    //Render chunk data
    std::vector<RenderObject> _renderedChunks;
    std::vector<RenderObject> _transparentObjects;

    std::unordered_set<ChunkCoord> _worldChunks;
    std::unordered_set<ChunkCoord> _oldWorldChunks;
    bool _initLoad{true};

    ChunkManager(VulkanEngine& renderer);

    ~ChunkManager();

    void updatePlayerPosition(int x, int z);

    Chunk* get_chunk(ChunkCoord coord);

private:
    void updateWorldState();
    std::pair<std::vector<ChunkCoord>, std::vector<ChunkCoord>> queueWorldUpdate(int changeX, int changeZ);
    void worldUpdate();
    void meshChunk(int threadId);

    std::optional<std::array<Chunk*, 8>> get_chunk_neighbors(ChunkCoord coord);

    int get_chunk_index(ChunkCoord coord);
    void add_chunk(ChunkCoord coord, std::unique_ptr<Chunk>&& chunk);

    bool _updatingWorldState = false;
    int _viewDistance;
    size_t _maxChunks;
    size_t _maxThreads;
    ChunkCoord _lastPlayerChunk = {0, 0};

    TerrainGenerator& _terrainGenerator;
    VulkanEngine& _renderer;

    std::queue<WorldUpdateJob> _worldUpdateQueue;
    moodycamel::BlockingConcurrentQueue<Chunk*> _chunkGenQueue{_maxChunks};
    moodycamel::BlockingConcurrentQueue<std::pair<Chunk*, std::array<Chunk*, 8>>> _chunkMeshQueue;
    // moodycamel::ConcurrentQueue<std::shared_ptr<Chunk>> _chunkSwapQueue;

    std::vector<std::thread> _workers;
    std::thread _updateThread;

    std::mutex _mutexWorld;
    std::mutex _mutexWork;

    std::condition_variable _cvWorld;
    std::condition_variable _cvWork;
    std::atomic<bool> _workComplete{true};

    std::barrier<> _sync_point;

    std::atomic<bool> _running;
};